#include "bridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "gameswf/gameswf_action.h"
#include "gameswf/gameswf_log.h"

using gameswf::as_object;
using gameswf::as_object_interface;
using gameswf::as_value;
using gameswf::fn_call;

namespace obf2::swf {
namespace {

const Host* g_host = nullptr;
std::vector<std::string>* g_commands = nullptr;

std::string arg(const fn_call& fn, int i) {
  return fn.nargs > i ? std::string(fn.arg(i).to_string()) : std::string();
}
void setString(const fn_call& fn, const std::string& text) { fn.result->set_string(text.c_str()); }
std::string localize(const std::string& key) {
  return g_host && g_host->localize ? g_host->localize(key) : key;
}
std::string readText(const std::string& path) {
  if (!g_host || !g_host->readFile) return {};
  const auto bytes = g_host->readFile(path);
  if (!bytes) return {};
  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

// --- a bridge object -----------------------------------------------------
//
// Real methods are ordinary members. A name it does not have answers with a
// function that does nothing and returns `false` — in ActionScript 2
// `undefined` misleads comparisons and arithmetic, `false` is an honest "no"
// (no servers, no battle running). Each such name is printed once: the list
// is the work still to do.
void stub(const fn_call& fn) { fn.result->set_bool(false); }

struct BridgeObject : as_object {
  std::string name;
  explicit BridgeObject(std::string n) : name(std::move(n)) {}
  bool get_member(const tu_stringi& member, as_value* val) override {
    if (as_object::get_member(member, val)) return true;
    const std::string asked = member.c_str();
    if (asked.rfind("__", 0) == 0 || asked == "addProperty") return false;
    static std::set<std::string> reported;
    if (reported.insert(name + "." + asked).second) {
      std::printf("menu bridge: %s.%s is not made yet\n", name.c_str(), asked.c_str());
    }
    val->set_as_c_function_ptr(stub);
    return true;
  }
};

// --- ServerSettings, GeneralSettings -----------------------------------------
//
// Every getter and setter of SwiffPlayer's ServerSettings (registered at
// 0x10066760, 98 methods) and GeneralSettings (0x10033b50, 60;
// `tools/swiff_methods.py <dll> <any method>` lists them) looks up an engine
// object by name — `GameServerSettings`, `GeneralSettings` — and calls one of
// its methods: setTeamRatio (0x10063b50) passes its argument as a number to the
// object's +0x17c, getTeamRatio (0x10064eb0) returns +0x178's float as a
// Number, setMaxBots (0x10031f40) rounds its argument and calls +0xb4. The same
// objects are what the profile's ServerSettings.con and General.con write. So
// a setter here is the console line `<Object>.set<Name> <value>`, and a getter
// reads what the engine keeps of it (Engine::setting).
//
// What is not read: which getters answer a string or a boolean rather than a
// number (we answer a number whenever the text is one), what `apply`, `reset`
// and `cancel` do (nothing here), and where a menu name differs from the
// console's — `CoopBotDiff` is taken as `CoopBotDifficulty`, the one pair
// whose field is plainly the same; `MapIconTransparency` against the .con's
// `MapIconAlphaTransparency` is left as it is.
std::string g_settingsMember;  // the method CallMethod just looked up
std::string g_settingsObject;  // and the engine object it belongs to

std::string settingsKey(std::string name) {
  name = name.substr(3);
  if (name == "CoopBotDiff") return "CoopBotDifficulty";
  return name;
}

// What each getter hands the movie, read per method with
// `tools/swiff_accessors.py` (the descriptor type its body builds: a String, a
// Number or a Boolean), and the arithmetic some of them do:
// * the three transparencies (getHUDTransparency 0x10033840,
//   getMinimapTransparency 0x100337b0, getMapIconTransparency 0x10033720)
//   answer 1 − v/255 (fmul 1/255, fsubr 1); their setters store 255 − x·255;
// * the three colours (getCrosshairColor 0x100334a0, Buddytag, Squadtag) pack
//   the engine's four values — the .con's `r g b a` (`setCrosshairColor 255
//   255 0 204`) — into a<<24 | r<<16 | g<<8 | b; their setters pass the
//   movie's arguments on in the same order (setCrosshairColor 0x100323f0);
// * ServerSettings.setCoopBotDiff (0x10063aa0) also sets GeneralSettings'
//   bot skill to its value × 0.01 (+0xc4) beside GameServerSettings' own
//   (+0x1ac).
enum class Kind { Number, Boolean, String };
Kind getterKind(const std::string& object, const std::string& name) {
  static const std::set<std::string> booleans = {
      // GeneralSettings
      "MinimapRotate", "ViewIntroMovie", "OutOfVoting", "PlayedVOHelp", "ConfirmQuit", "UseBots",
      "MaxBotsIncludeHumans", "AllowPunkBuster", "ItemSelectionReverseItems", "AutoReload", "AutoReady",
      // GameServerSettings
      "AllowNATNegotation", "AutoBalanceTeam", "AutoRecord", "FriendlyFire", "Internet", "NoVehicles",
      "PunishTeamKills", "SvPunkBuster", "VoipEnabled", "VoipServerRemote"};
  static const std::set<std::string> strings = {
      "SortKey", "ServerFilter", "BFTVSaveDirectory",
      "AdminScript", "DemoDownloadURL", "DemoHook", "DemoIndexURL", "GameMode", "InterfaceIP", "Password",
      "ServerName", "SponsorLogoURL", "SponsorText", "VoipServerRemoteIP"};
  (void)object;
  if (booleans.count(name)) return Kind::Boolean;
  if (strings.count(name)) return Kind::String;
  return Kind::Number;
}
bool isTransparency(const std::string& name) {
  return name == "HUDTransparency" || name == "MinimapTransparency" || name == "MapIconTransparency";
}
bool isColour(const std::string& name) {
  return name == "CrosshairColor" || name == "BuddytagColor" || name == "SquadtagColor";
}

void settingsGet(const fn_call& fn) {
  // `checkPlayedVOHelp` is a getter by its body, though not by its name.
  const std::string member = g_settingsMember == "checkPlayedVOHelp" ? "getPlayedVOHelp" : g_settingsMember;
  const std::string key = settingsKey(member);
  const std::string value =
      g_host && g_host->setting ? g_host->setting(g_settingsObject, key) : std::string();
  if (isTransparency(key)) {
    fn.result->set_double(1.0 - std::strtod(value.c_str(), nullptr) * (1.0 / 255.0));
    return;
  }
  if (isColour(key)) {
    unsigned c[4] = {0, 0, 0, 0};
    std::sscanf(value.c_str(), "%u %u %u %u", &c[0], &c[1], &c[2], &c[3]);
    fn.result->set_double(static_cast<double>(((c[3] & 255u) << 24) | ((c[0] & 255u) << 16) |
                                              ((c[1] & 255u) << 8) | (c[2] & 255u)));
    return;
  }
  switch (getterKind(g_settingsObject, key)) {
    case Kind::Boolean:
      fn.result->set_bool(!value.empty() && value != "0" && value != "false");
      return;
    case Kind::String:
      setString(fn, value);
      return;
    case Kind::Number:
      fn.result->set_double(std::strtod(value.c_str(), nullptr));
      return;
  }
}

void settingsSet(const fn_call& fn) {
  if (!g_host || !g_host->console || fn.nargs < 1) return;
  const std::string key = settingsKey(g_settingsMember);
  std::string line = g_settingsObject + ".set" + key;
  for (int i = 0; i < fn.nargs; ++i) {
    const as_value& v = fn.arg(i);
    std::string text;
    if (isTransparency(key)) {
      char buffer[32];
      std::snprintf(buffer, sizeof buffer, "%g", 255.0 - v.to_number() * 255.0);
      text = buffer;
    } else {
      // A flag is written the way the .con writes it: 1 or 0. A text with a
      // space is quoted, as the profile's files quote it.
      text = v.get_type() == as_value::BOOLEAN ? (v.to_bool() ? "1" : "0") : std::string(v.to_string());
      if (text.empty() || text.find(' ') != std::string::npos) text = "\"" + text + "\"";
    }
    line += " " + text;
  }
  g_host->console(line);
  if (g_settingsObject == "GameServerSettings" && key == "CoopBotDifficulty") {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", fn.arg(0).to_number() * 0.01);
    g_host->console(std::string("GeneralSettings.setBotSkill ") + buffer);
  }
}

void settingsNothing(const fn_call&) {}

struct SettingsObject : BridgeObject {
  std::string engineObject;
  SettingsObject(const char* menuName, const char* engineName) : BridgeObject(menuName), engineObject(engineName) {}
  bool get_member(const tu_stringi& member, as_value* val) override {
    const std::string asked = member.c_str();
    if (as_object::get_member(member, val)) return true;  // our own methods first
    if (asked == "checkPlayedVOHelp" ||
        (asked.size() > 3 && (asked.rfind("get", 0) == 0 || asked.rfind("set", 0) == 0))) {
      g_settingsMember = asked;
      g_settingsObject = engineObject;
      val->set_as_c_function_ptr(asked[0] == 's' ? settingsSet : settingsGet);
      return true;
    }
    if (asked == "apply" || asked == "reset" || asked == "cancel") {
      val->set_as_c_function_ptr(settingsNothing);
      return true;
    }
    return BridgeObject::get_member(member, val);
  }
};

// --- Locale ---------------------------------------------------------------

// `Locale.getString(key)` → the line from the localisation; a missing key
// comes back as itself, which is how the game shows it.
void localeGetString(const fn_call& fn) { setString(fn, localize(arg(fn, 0))); }
void localeLanguage(const fn_call& fn) { setString(fn, g_host ? g_host->language : "english"); }
// `checkXMLStatus` — our dictionary is ready at once.
void localeReady(const fn_call& fn) { fn.result->set_bool(true); }

// Deferred captions. The stock components (`TextContainer`, symbol 117) call
// `setLocID` → `addDelayedInstance(field, key)` in `createChildren`, and the
// very next `arrange()` writes the key itself into the field; the text is
// meant to be handed out later by `assignDelayedInstances`. So this is a real
// queue, flushed after every frame.
struct Delayed {
  smart_ptr<as_object_interface> field;
  std::string key;
};
std::vector<Delayed>& delayed() {
  static std::vector<Delayed> queue;
  return queue;
}

void localeAddDelayed(const fn_call& fn) {
  if (fn.nargs < 2) return;
  as_object_interface* field = fn.arg(0).to_object();
  const std::string key = arg(fn, 1);
  if (field == nullptr || key.empty()) return;
  if (std::getenv("OBF2_SWF_TRACE")) {
    std::printf("[bridge] addDelayedInstance %s (%s)\n", key.c_str(),
                field->get_text_value() ? field->get_text_value() : "no text value");
  }
  delayed().push_back(Delayed{field, key});
}

void localeAssignDelayed(const fn_call&) { flushDelayedCaptions(); }

// --- Logic ------------------------------------------------------------------

// `Logic.active()` — the first thing the menu asks. While "no", components
// stay in author mode and show their keys.
void logicActive(const fn_call& fn) { fn.result->set_bool(true); }

// --- what the host keeps for the menu (SwiffPlayer.dll, Logic and others) ----
//
// These methods only hand a value to the host or take one back; the host's
// side is a field each, kept here.
struct HostState {
  std::string frameLabel;  // setFrameLabel → +0xc8, getFrameLabel ← +0xc4
  int activateReason = 0;  // getActivateReason ← +0xbc, then +0xc0(0)
  bool async = false;      // setAsync → the host's +0x80
  bool scroller = false;   // enableScroller → +0x330
  std::string browser;     // Multiplay.setBrowser → +0x138
};
HostState& hostState() {
  static HostState state;
  return state;
}

// 0x1004cc30: a Boolean from the host.
void isGameRunning(const fn_call& fn) {
  fn.result->set_bool(g_host && g_host->gameRunning && g_host->gameRunning());
}
// 0x1004ccf0: a Number, and the reason is cleared once read. Who sets it (the
// host's setter behind Logic.setActivateReason, 0x1004cb70) is the game; ours
// never does, so it stays 0.
void getActivateReason(const fn_call& fn) {
  fn.result->set_double(hostState().activateReason);
  hostState().activateReason = 0;
}
void setActivateReason(const fn_call& fn) {
  hostState().activateReason = fn.nargs > 0 ? static_cast<int>(fn.arg(0).to_number()) : 0;
}
// 0x1004cd70 / 0x1004cbd0: a String the host keeps for the menu.
void getFrameLabel(const fn_call& fn) { setString(fn, hostState().frameLabel); }
void setFrameLabel(const fn_call& fn) { hostState().frameLabel = arg(fn, 0); }
// 0x1004d450.
void isCapsLockPressed(const fn_call& fn) { fn.result->set_bool(g_host && g_host->capsLock && g_host->capsLock()); }
// 0x1004cef0, 0x1004cf30: flags handed to the host; what the host does with
// them is not read.
void setAsync(const fn_call& fn) { hostState().async = fn.nargs > 0 && fn.arg(0).to_bool(); }
void enableScroller(const fn_call& fn) { hostState().scroller = fn.nargs > 0 && fn.arg(0).to_bool(); }
// Multiplay.setBrowser (0x10051ea0) names the server browser; stopBrowser
// (0x10050bc0) stops the browser of that name — ours has none running.
void setBrowser(const fn_call& fn) { hostState().browser = arg(fn, 0); }
void stopBrowser(const fn_call&) {}
// Sound.playSound (0x10068f70): the host plays the menu sound by name (+0x188);
// when it does not, the answer is -1 and otherwise nothing. Ours has no sound
// output yet, so it is always -1.
void playSound(const fn_call& fn) { fn.result->set_double(-1.0); }
// Sound.voipSetEnable (0x100692b0) and voipStopTest (0x10069620) do nothing
// and answer nothing until VoIP is initialised (0x100f8b4c, 0x100f8b4d);
// ours never initialises it.
void voipNotInitialised(const fn_call&) {}
// VideoSettings.isRunningWithUnsupportedDrivers (0x1006bd60): the host's
// byte +0x324 as a Boolean. Ours runs no driver check: false.
void unsupportedDrivers(const fn_call& fn) { fn.result->set_bool(false); }
// Logic.setDecimals(value, decimals) (0x1004d640): the value printed with
// "%.<decimals>f" (the format string is built from "%%.%df"), as a String.
void setDecimals(const fn_call& fn) {
  const double value = fn.nargs > 0 ? fn.arg(0).to_number() : 0.0;
  const int decimals = fn.nargs > 1 ? static_cast<int>(fn.arg(1).to_number()) : 0;
  char text[64];
  std::snprintf(text, sizeof text, "%.*f", decimals < 0 ? 0 : decimals, static_cast<double>(static_cast<float>(value)));
  setString(fn, text);
}
// Logic.getAsync (0x1004d210): the host's +0x7c, the flag setAsync gave.
void getAsync(const fn_call& fn) { fn.result->set_bool(hostState().async); }
// Multiplay.getBrowser (0x10054da0): the name setBrowser gave (+0x134).
void getBrowser(const fn_call& fn) { setString(fn, hostState().browser); }
// Multiplay.isBrowsing (0x10052300): false when no browser of that name runs
// (+0x7c finds none), otherwise its +0x1c. Ours runs none.
void isBrowsing(const fn_call& fn) { fn.result->set_bool(false); }
// Multiplay.isDownloadingDemo (0x10053ee0): the demo manager's (+0x44) +0x28.
// Ours downloads none.
void isDownloadingDemo(const fn_call& fn) { fn.result->set_bool(false); }
// Multiplay.getNumDemoBookmarks (0x10053e50): LocalProfileManager's +0xa8, a
// Number. Where the bookmarks come from is not read; the profile folders hold
// no file of them, so 0.
void numDemoBookmarks(const fn_call& fn) { fn.result->set_double(0.0); }
// Multiplay.getToggleFilter(n) (0x10052940): GeneralSettings' +0x10c(n) as a
// Boolean; setToggleFilter(n, on, label) (0x100571b0): +0x110(n, on), the
// label kept beside it. The profile keeps the filters as one number
// (`GeneralSettings.setToggleFilters 63616`), so filter n is taken as bit n of
// it — inferred from the one number, the field's layout is not read.
unsigned toggleFilters() {
  if (!g_host || !g_host->setting) return 0;
  return static_cast<unsigned>(std::strtoul(g_host->setting("GeneralSettings", "ToggleFilters").c_str(), nullptr, 10));
}
void getToggleFilter(const fn_call& fn) {
  const int n = fn.nargs > 0 ? static_cast<int>(fn.arg(0).to_number()) : 0;
  fn.result->set_bool(n >= 0 && n < 32 && ((toggleFilters() >> n) & 1u) != 0);
}
void setToggleFilter(const fn_call& fn) {
  if (!g_host || !g_host->console || fn.nargs < 2) return;
  const int n = static_cast<int>(fn.arg(0).to_number());
  if (n < 0 || n >= 32) return;
  unsigned mask = toggleFilters();
  mask = fn.arg(1).to_bool() ? (mask | (1u << n)) : (mask & ~(1u << n));
  g_host->console("GeneralSettings.setToggleFilters " + std::to_string(mask));
}
void cacheCredits(const fn_call&) {
  if (g_host && g_host->cacheCredits) g_host->cacheCredits();
}
// The fade speed is whatever credits.con set (`swiffHost.setScrollFadeSpeed
// 200`); before it ran, 0 — the host's field is not read for its start value.
void scrollFadeSpeed(const fn_call& fn) {
  fn.result->set_double(g_host && g_host->scrollFadeSpeed ? g_host->scrollFadeSpeed() : 0);
}
// Multiplay.updateFilter / startBrowse: they act on the server browser, which
// ours does not have; nothing happens.
void browserNothing(const fn_call&) {}
// Names the movie calls that SwiffPlayer.dll does not register at all —
// `Logic.getCommandline` (the DLL's is `getCommandLine`, 0x1004d4b0),
// `Locale.setFlaName`, `Locale.setDefaultLang`: undefined, as a missing method
// answers in the original.
void absent(const fn_call&) {}

// `Logic.getModVersion()` (0x1004d520) is the **player's** version, from the
// engine (+0x1a0), the same call as `General.getHostVersion`.
void hostVersion(const fn_call& fn) { setString(fn, g_host ? g_host->hostVersion : ""); }

// The string storage — memory between the menu's pages (0x1004d0e0, 0x1004db60,
// 0x1004d170). None of its keys is in BF2.exe, the data or the profile, so it
// lives for the session only.
std::map<std::string, std::string>& storage() {
  static std::map<std::string, std::string> values;
  return values;
}
void storageSet(const fn_call& fn) { storage()[arg(fn, 0)] = arg(fn, 1); }
void storageGet(const fn_call& fn) {
  const auto it = storage().find(arg(fn, 0));
  // The default is the second argument; without one an empty string, which
  // goes on into comparisons and picture paths better than `undefined`.
  setString(fn, it != storage().end() ? it->second : arg(fn, 1));
}
void storageErase(const fn_call& fn) { storage().erase(arg(fn, 0)); }

void quit(const fn_call&) {
  if (g_commands) g_commands->push_back("quit");
}

// --- the mod ----------------------------------------------------------------

std::string modField(const std::string& name) {
  static const std::string text = readText("mod.desc");
  const std::string open = "<" + name + ">";
  const std::string close = "</" + name + ">";
  const auto from = text.find(open);
  if (from == std::string::npos) return {};
  const auto to = text.find(close, from + open.size());
  if (to == std::string::npos) return {};
  std::string value = text.substr(from + open.size(), to - from - open.size());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(0, 1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  return value;
}
void modVersion(const fn_call& fn) { setString(fn, modField("version")); }
void modTitle(const fn_call& fn) { setString(fn, modField("title")); }
void modDesc(const fn_call& fn) { setString(fn, modField("desc")); }
void modUrl(const fn_call& fn) { setString(fn, modField("url")); }
void modLogo(const fn_call& fn) { setString(fn, modField("logo")); }
// The way the game writes it in Init.con: `game.setActiveModId "mods/bf2"`.
void modPath(const fn_call& fn) { setString(fn, "mods/bf2"); }
void modName(const fn_call& fn) { setString(fn, "bf2"); }

// --- profiles and login -------------------------------------------------------

// The profile logged into; empty: none. `Profile.getLoginStatus()`
// (0x1005f390) answers 2 for a network profile, 1 for a local one, 0 for none;
// while 0 the menu drives to its login page.
std::string& loggedIn() {
  static std::string id;
  return id;
}

const Host::Profile* defaultProfile() {
  if (!g_host || g_host->profiles.empty()) return nullptr;
  for (const auto& p : g_host->profiles) {
    if (p.id == g_host->defaultUser) return &p;
  }
  return &g_host->profiles.front();
}

void playerName(const fn_call& fn) {
  const Host::Profile* p = defaultProfile();
  setString(fn, p ? p->name : "");
}
void localProfiles(const fn_call& fn) {
  fn.result->set_int(g_host ? static_cast<int>(g_host->profiles.size()) : 0);
}
// `Profile.getNamePrefix()` — Global.con's `setNamePrefix`, not the folder.
void namePrefix(const fn_call& fn) { setString(fn, g_host ? g_host->namePrefix : ""); }

// `Profile.loginOfflineAccount(number)` (0x1005e850) takes the profile number.
// The engine loads the profile (`loadCurrentProfile`, BF2.exe 0x4ab1d0); the
// movie only learns whether it worked.
void loginOffline(const fn_call& fn) {
  const long wanted = fn.nargs > 0 ? std::lround(fn.arg(0).to_number()) : -1;
  std::string found;
  if (g_host) {
    for (const auto& p : g_host->profiles) {
      if (std::atol(p.id.c_str()) == wanted) found = p.id;
    }
  }
  if (!found.empty() && g_commands) g_commands->push_back("login " + found);
  loggedIn() = found;
  fn.result->set_bool(!found.empty());
}
void loginStatus(const fn_call& fn) { fn.result->set_int(loggedIn().empty() ? 0 : 1); }
void logout(const fn_call&) { loggedIn().clear(); }

// --- levels -------------------------------------------------------------------

// Everything the menu shows about a level is its own `Info/<dir>.desc`.
struct MapMode {
  std::string mode;
  int size = 0;
  bool ai = false;
  std::string locid;  // <maptype locid="GAMEMODE_DESCRIPTION_assault">
};
struct MapInfo {
  std::string id, path, name, briefing;
  std::vector<MapMode> modes;
};

std::string attribute(const std::string& chunk, const std::string& name) {
  const std::string key = name + "=\"";
  const auto from = chunk.find(key);
  if (from == std::string::npos) return {};
  const auto to = chunk.find('"', from + key.size());
  if (to == std::string::npos) return {};
  return chunk.substr(from + key.size(), to - from - key.size());
}

std::string tagValue(const std::string& xml, const std::string& tag) {
  const auto from = xml.find("<" + tag);
  if (from == std::string::npos) return {};
  const auto head = xml.find('>', from);
  const auto to = xml.find("</" + tag + ">", from);
  if (head == std::string::npos || to == std::string::npos || to < head) return {};
  std::string value = xml.substr(head + 1, to - head - 1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(0, 1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  return value;
}

MapInfo parseDesc(const std::string& xml, const std::string& directory) {
  MapInfo map;
  const auto headEnd = xml.find('>');
  map.id = attribute(xml.substr(0, headEnd == std::string::npos ? 0 : headEnd), "gsid");
  // The engine spells level paths in lower case: `maplist.append "dalian_plant" …`.
  map.path = directory;
  std::transform(map.path.begin(), map.path.end(), map.path.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  map.name = tagValue(xml, "name");
  if (map.name.empty()) map.name = directory;
  const auto briefing = xml.find("<briefing");
  if (briefing != std::string::npos) map.briefing = attribute(xml.substr(briefing, 200), "locid");
  std::size_t cursor = 0;
  while ((cursor = xml.find("<mode ", cursor)) != std::string::npos) {
    const std::string mode = attribute(xml.substr(cursor, 80), "type");
    const auto end = xml.find("</mode>", cursor);
    const std::size_t stop = end == std::string::npos ? xml.size() : end;
    std::size_t inner = cursor;
    while ((inner = xml.find("<maptype", inner)) != std::string::npos && inner < stop) {
      const auto tagEnd = xml.find('>', inner);
      const std::string chunk = xml.substr(inner, (tagEnd == std::string::npos ? stop : tagEnd) - inner);
      map.modes.push_back(MapMode{mode, std::atoi(attribute(chunk, "players").c_str()),
                                  attribute(chunk, "ai") == "1", attribute(chunk, "locid")});
      inner = tagEnd == std::string::npos ? stop : tagEnd;
    }
    cursor = stop + 1;
  }
  return map;
}

const std::vector<MapInfo>& maps() {
  static std::vector<MapInfo> all = [] {
    std::vector<MapInfo> out;
    if (!g_host) return out;
    for (const auto& dir : g_host->levelDirectories) {
      const std::string xml = readText("Levels/" + dir + "/Info/" + dir + ".desc");
      if (!xml.empty()) out.push_back(parseDesc(xml, dir));
    }
    std::sort(out.begin(), out.end(), [](const MapInfo& a, const MapInfo& b) { return a.name < b.name; });
    return out;
  }();
  return all;
}

const MapInfo* mapByPath(std::string path) {
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const auto& m : maps()) {
    if (m.path == path) return &m;
  }
  return nullptr;
}

// The game mode the list is filtered by; the singleplayer page sets gpm_cq.
std::string& gameMode() {
  static std::string mode = "gpm_cq";
  return mode;
}
// `GeneralSettings.setUseBots` — with bots on only AI-capable entries are
// listed (SwiffPlayer.dll 0x1002b280). The flag is the engine's
// GeneralSettings' (the profile's General.con carries it too).
bool useBots() {
  if (!g_host || !g_host->setting) return false;
  const std::string v = g_host->setting("GeneralSettings", "UseBots");
  return !v.empty() && v != "0" && v != "false";
}
void setFilterMode(const fn_call& fn) { gameMode() = arg(fn, 0); }
void getFilterMode(const fn_call& fn) { setString(fn, gameMode()); }

void mapNameFromId(const fn_call& fn) {
  const MapInfo* m = mapByPath(arg(fn, 0));
  setString(fn, m ? m->name : arg(fn, 0));
}
void mapDescriptionFromId(const fn_call& fn) {
  const MapInfo* m = mapByPath(arg(fn, 0));
  setString(fn, m ? localize(m->briefing) : "");
}
void mapPathFromName(const fn_call& fn) {
  const std::string name = arg(fn, 0);
  for (const auto& m : maps()) {
    if (m.name.size() == name.size() &&
        std::equal(m.name.begin(), m.name.end(), name.begin(),
                   [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
      setString(fn, m.path);
      return;
    }
  }
  setString(fn, name);
}
// `GAMEMODE_<id>` in the lexicon; no key for gpm_cq exists, so where the
// engine takes that name from is not established and the id shows.
// Multiplay.getGameModeFromId (0x10053710): "Singleplayer" for sp1, sp2, sp3;
// otherwise "Co-op" when the host's +0x164 answers "gpm_coop" for the id, and
// "Conquest" for anything else — plain strings, not localisation keys. What
// +0x164 makes of an id is not read; the id itself stands in for it.
void gameModeFromId(const fn_call& fn) {
  const std::string id = arg(fn, 0);
  if (id == "sp1" || id == "sp2" || id == "sp3") {
    setString(fn, "Singleplayer");
  } else {
    setString(fn, id == "gpm_coop" ? "Co-op" : "Conquest");
  }
}

// --- the loading screen: endOfRound.swf ---------------------------------------
//
// What the movie asks while a level loads (its frame code: `hasMap`, then the
// `briefing` page with `$/Levels/<path>/Info/loadMap.png`, the name, the
// briefing and a bar from `Client.getLoadProgress`, compared with 0.95).
// SwiffPlayer.dll's bodies read the host's fields (`tools/swiff_accessors.py`
// on getMapName lists them): the level the host is loading. Ours answer from
// Host::loading and the level's own .desc; the host's fields themselves are
// not read, so where an answer is a value of ours it says so.
const Host::Loading* loadingState() { return g_host ? &g_host->loading : nullptr; }
const MapInfo* loadingMap() {
  const Host::Loading* l = loadingState();
  return l && !l->mapPath.empty() ? mapByPath(l->mapPath) : nullptr;
}
// EndOfRound.hasMap (0x100102d0): a map is named.
void eorHasMap(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  fn.result->set_bool(l && !l->mapPath.empty());
}
void eorMapPath(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  setString(fn, l ? l->mapPath : std::string());
}
// getMapName (0x10010860): the level's name, "Unknown Map" when it is not found.
void eorMapName(const fn_call& fn) {
  const MapInfo* m = loadingMap();
  setString(fn, m ? m->name : std::string("Unknown Map"));
}
void eorMapSize(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  fn.result->set_double(l ? l->mapSize : 0);
}
// getMapMaxPlayers (0x100109a0, the host's +0x60): the mode's player count is
// all the .desc gives; taken for it.
void eorMapMaxPlayers(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  fn.result->set_double(l ? l->mapSize : 0);
}
void eorMapGameMode(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  setString(fn, l ? l->gameMode : std::string());
}
// getMapGameModeName (0x10010b20): the same names getGameModeFromId gives.
void eorMapGameModeName(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  const std::string id = l ? l->gameMode : std::string();
  setString(fn, id == "sp1" || id == "sp2" || id == "sp3" ? "Singleplayer"
                : id == "gpm_coop"                         ? "Co-op"
                                                           : "Conquest");
}
// getMapDescription (0x10010a00): the briefing's text (the .desc's locid).
void eorMapDescription(const fn_call& fn) {
  const MapInfo* m = loadingMap();
  setString(fn, m && !m->briefing.empty() ? localize(m->briefing) : std::string());
}
// getMapGameModeDescription (0x100114c0): the mode's text — the locid of the
// .desc's <maptype> of that mode and size. sp1..sp3 take gpm_cq's, as the
// body's strings suggest ('sp1', 'sp2', 'sp3', 'gpm_cq'); inferred.
void eorMapGameModeDescription(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  const MapInfo* m = loadingMap();
  std::string text;
  if (l && m) {
    std::string mode = l->gameMode;
    if (mode == "sp1" || mode == "sp2" || mode == "sp3") mode = "gpm_cq";
    for (const auto& entry : m->modes) {
      if (entry.mode == mode && entry.size == l->mapSize && !entry.locid.empty()) {
        text = localize(entry.locid);
        break;
      }
    }
  }
  setString(fn, text);
}
// The previous round's questions: no round has been played before the first
// load, so nothing — and the movie, told it is the first load, does not show
// that page.
void eorEmptyString(const fn_call& fn) { setString(fn, ""); }
void eorZero(const fn_call& fn) { fn.result->set_double(0.0); }
void eorNoTeam(const fn_call& fn) { fn.result->set_double(-1.0); }  // the bodies' default (fld -1)
// getKitNameFromId (0x100106d0): its table of kit names, in the order its
// strings lie.
void eorKitName(const fn_call& fn) {
  static const char* kNames[] = {"antitank", "assault", "engineer", "medic", "specops", "support", "sniper"};
  const int id = fn.nargs > 0 ? static_cast<int>(fn.arg(0).to_number()) : -1;
  setString(fn, id >= 0 && id < 7 ? kNames[id] : "");
}
// Client.getLoadProgress (0x1000d810, the host's +0x94): 0..1.
void clientLoadProgress(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  fn.result->set_double(l && l->progress ? l->progress() : 0.0);
}
// Client.getEORType (0x1000d870, +0x98): which end-of-round screen. What value
// a first load has is not read; 0.
void clientEORType(const fn_call& fn) { fn.result->set_double(0.0); }
// Client.getCurrentLoadFile (0x1000d9d0, +0xa0): the file being read.
void clientCurrentLoadFile(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  setString(fn, l && l->currentFile ? l->currentFile() : std::string());
}
// Logic.getIsLoading (0x1004d980), isFirstTimeLoading (0x1004d910),
// readyToPlay (0x1004de90: the host's +0x2c — the movie is done with the
// screen), isAbortLoadLevel (0x1004d3e0: no abort in ours), isLoadingDemo
// (0x1004d270: no demos), getNumRoundsLeft (0x1004d2e0: not read, 0).
void logicIsLoading(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  fn.result->set_bool(l && l->active && l->active());
}
void logicFirstTimeLoading(const fn_call& fn) {
  const Host::Loading* l = loadingState();
  fn.result->set_bool(l && l->firstLoad);
}
void logicReadyToPlay(const fn_call&) {
  const Host::Loading* l = loadingState();
  if (l && l->readyToPlay) l->readyToPlay();
}
void logicFalse(const fn_call& fn) { fn.result->set_bool(false); }
// Logic.getNumRoundsLeft (0x1004d2e0, the host's +0x358): the rounds left on
// this map. endOfRound.swf's goFurther shows its JOIN GAME button only while
// it is above 0 (`goFurther.flush`: m_interactive && getNumRoundsLeft() > 0 →
// "interactive"). Before the first round of a map all of them are left: the
// server's RoundsPerMap (the profile's ServerSettings.con, 99). That +0x358
// holds this count is inferred from the name, not read.
void numRoundsLeft(const fn_call& fn) {
  double rounds = 1.0;
  if (g_host && g_host->setting) {
    const std::string v = g_host->setting("GameServerSettings", "RoundsPerMap");
    if (!v.empty()) rounds = std::strtod(v.c_str(), nullptr);
  }
  fn.result->set_double(rounds);
}
// Multiplay.get/setHasNewBookmark (0x10052a80 / 0x100516b0): the demo
// manager's (+0x44) flag, +0x40 / +0x44. Ours has no demos; the flag is kept.
bool& newBookmark() {
  static bool flag = false;
  return flag;
}
void getHasNewBookmark(const fn_call& fn) { fn.result->set_bool(newBookmark()); }
void setHasNewBookmark(const fn_call& fn) { newBookmark() = fn.nargs > 0 && fn.arg(0).to_bool(); }
// VideoSettings.isOptimzingShaders (0x1006bdd0, the host's +0x31c): ours
// compiles no shaders at run time.
void isOptimizingShaders(const fn_call& fn) { fn.result->set_bool(false); }

struct MapListEntry {
  std::string path, mode;
  int size = 0;
};
std::vector<MapListEntry>& mapList() {
  static std::vector<MapListEntry> list;
  return list;
}
void clearMapList(const fn_call&) { mapList().clear(); }
void addMapToMapList(const fn_call& fn) {
  std::string mode = arg(fn, 1);
  if (mode.empty()) mode = gameMode();
  mapList().push_back(MapListEntry{arg(fn, 0), mode, fn.nargs > 2 ? static_cast<int>(fn.arg(2).to_number()) : 0});
}
// `Multiplay.createServer()` — the level the list holds is started.
void createServer(const fn_call& fn) {
  if (mapList().empty()) {
    fn.result->set_bool(false);
    return;
  }
  const auto& e = mapList().front();
  if (g_commands) {
    g_commands->push_back("level " + e.path + " " + e.mode + " " + std::to_string(e.size));
  }
  fn.result->set_bool(true);
}

// --- lists: dice.General ---------------------------------------------------------

as_object* object() { return new as_object(); }
void put(as_object* o, const char* name, const as_value& v) { o->set_member(name, v); }

// The first entry is a header (`listArray[0][0]` is the list object, and the
// header carries `totalCount`); rows are arrays — `[0]` the entry object,
// `[1..]` the column values (`ColumnList.fillList`, symbol 42).
as_object* listWithHeader(const char* id, int total) {
  as_object* all = gameswf::new_array();
  as_object* listObject = object();
  put(listObject, "id", as_value(id));
  as_object* header = gameswf::new_array();
  gameswf::array_append(header, as_value(listObject));
  put(header, "totalCount", as_value(static_cast<double>(total)));
  gameswf::array_append(all, as_value(header));
  return all;
}

void installedMaps(const fn_call& fn) {
  std::vector<std::pair<const MapInfo*, const MapMode*>> rows;
  for (const auto& m : maps()) {
    for (const auto& mode : m.modes) {
      if (mode.mode == gameMode() && (!useBots() || mode.ai)) rows.push_back({&m, &mode});
    }
  }
  as_object* all = listWithHeader("installedMaps", static_cast<int>(rows.size()));
  for (const auto& [m, mode] : rows) {
    as_object* meta = object();
    put(meta, "id", as_value(m->id.c_str()));
    put(meta, "path", as_value(m->path.c_str()));
    put(meta, "mapSize", as_value(static_cast<double>(mode->size)));
    put(meta, "disabled", as_value(false));
    as_object* row = gameswf::new_array();
    gameswf::array_append(row, as_value(meta));
    gameswf::array_append(row, as_value(m->name.c_str()));
    gameswf::array_append(all, as_value(row));
  }
  fn.result->set_as_object_interface(all);
}

// `General.getListEntries(name)` (0x10022610): `localProfiles` (0x10026baa) and
// the maps; the rest are not made yet and come back empty.
void listEntries(const fn_call& fn) {
  const std::string name = arg(fn, 0);
  if (name == "installedMaps") {
    installedMaps(fn);
    return;
  }
  if (name != "localProfiles" || !g_host) {
    fn.result->set_as_object_interface(gameswf::new_array());
    return;
  }
  as_object* all = listWithHeader("localProfiles", static_cast<int>(g_host->profiles.size()));
  for (const auto& p : g_host->profiles) {
    const bool online = !p.gamespyNick.empty();
    as_object* meta = object();
    put(meta, "profileName", as_value(p.name.c_str()));
    put(meta, "password", as_value(""));
    put(meta, "id", as_value(p.id.c_str()));
    put(meta, "gamespy", as_value(online));
    put(meta, "disabled", as_value(false));
    // The "Online" column is the account icon's key, translated before it
    // goes in (0x10026db3); "Playername" the network name if any.
    const std::string icon = localize(online ? "ACCOUNT_online" : "ACCOUNT_offline");
    as_object* row = gameswf::new_array();
    gameswf::array_append(row, as_value(meta));
    gameswf::array_append(row, as_value(icon.c_str()));
    gameswf::array_append(row, as_value((online ? p.gamespyNick : p.name).c_str()));
    gameswf::array_append(all, as_value(row));
  }
  fn.result->set_as_object_interface(all);
}

// `getListRevision`/`flushList` (0x10019d80, 0x10019ba0): −1 for every list but
// the three server ones, which we do not have.
void listRevision(const fn_call& fn) { fn.result->set_double(-1.0); }

// --- installing ---------------------------------------------------------------------

struct Method {
  const char* name;
  gameswf::as_c_function_ptr fn;
};

BridgeObject* makeObject(const std::string& name, const std::vector<Method>& methods) {
  auto* o = new BridgeObject(name);
  for (const auto& m : methods) o->set_member(m.name, as_value(m.fn));
  return o;
}

}  // namespace

void flushDelayedCaptions() {
  auto& queue = delayed();
  if (queue.empty()) return;
  std::vector<Delayed> pending;
  pending.swap(queue);
  for (auto& d : pending) {
    // A field only this queue still holds has left the display list with its
    // page, and its parent pointer is gone with it: writing it then crashed
    // (`text` reads the parent's movie version). Such a caption is dropped.
    if (d.field != nullptr && d.field->get_ref_count() > 1) {
      d.field->set_member("text", as_value(localize(d.key).c_str()));
    }
  }
}

void installBridge(const Host* host, std::vector<std::string>* commands) {
  g_host = host;
  g_commands = commands;
  as_object* global = gameswf::get_global();

  // The seventeen objects SwiffPlayer registers, under their own names and
  // under `dice.bf2`.
  // A vector per object, not an initializer_list: a list's array dies with
  // the expression that made it, and the optimised build read freed memory.
  const std::vector<std::pair<std::string, std::vector<Method>>> objects = {
      {"ControlSettings", {}},
      {"EndOfRound",
       {{"hasMap", eorHasMap}, {"getMapPath", eorMapPath}, {"getMapName", eorMapName},
        {"getMapSize", eorMapSize}, {"getMapMaxPlayers", eorMapMaxPlayers},
        {"getMapGameMode", eorMapGameMode}, {"getMapGameModeName", eorMapGameModeName},
        {"getMapDescription", eorMapDescription}, {"getMapGameModeDescription", eorMapGameModeDescription},
        {"getPreviousMapPath", eorEmptyString}, {"getPreviousMapName", eorEmptyString},
        {"getPreviousMapSize", eorZero}, {"getPreviousMapGameMode", eorEmptyString},
        {"getPreviousMapGameModeName", eorEmptyString}, {"getPreviousTeamName", eorEmptyString},
        {"getSquadTeamName", eorEmptyString}, {"getTeamNameOfPlayerAtPosition", eorEmptyString},
        {"getMyTeam", eorNoTeam}, {"getWinningTeam", eorNoTeam}, {"getTicketHistorySum", eorZero},
        {"getKitNameFromId", eorKitName}}},
      {"Player", {}},
      {"Mod",
       {{"getModVersion", modVersion}, {"getModTitle", modTitle}, {"getModDesc", modDesc},
        {"getModUrl", modUrl}, {"getModLogo", modLogo}, {"getModPath", modPath},
        {"getModPathShort", modName}, {"getCurrentModName", modName}}},
      {"Cursor", {}},
      {"Sound",
       {{"playSound", playSound}, {"voipSetEnable", voipNotInitialised}, {"voipStopTest", voipNotInitialised}}},
      {"Client",
       {{"getPlayerName", playerName}, {"getLoadProgress", clientLoadProgress}, {"getEORType", clientEORType},
        {"getCurrentLoadFile", clientCurrentLoadFile}}},
      {"Options", {}},
      {"Profile",
       {{"getNumLocalProfiles", localProfiles}, {"getStatPlayerName", playerName},
        {"getActivePlayer", playerName}, {"getNamePrefix", namePrefix},
        {"loginOfflineAccount", loginOffline}, {"getLoginStatus", loginStatus}, {"logout", logout}}},
      {"Clans", {}},
      {"Multiplay",
       {{"setFilterGameMode", setFilterMode}, {"getFilterGameMode", getFilterMode},
        {"getSelectedFilterGameMode", getFilterMode}, {"getMapNameFromId", mapNameFromId},
        {"getMapDescriptionFromId", mapDescriptionFromId}, {"getMapPathFromName", mapPathFromName},
        {"getGameModeFromId", gameModeFromId}, {"clearMapList", clearMapList},
        {"addMapToMapList", addMapToMapList}, {"createServer", createServer},
        {"setBrowser", setBrowser}, {"stopBrowser", stopBrowser}, {"getBrowser", getBrowser},
        {"isBrowsing", isBrowsing}, {"isDownloadingDemo", isDownloadingDemo},
        {"getNumDemoBookmarks", numDemoBookmarks}, {"getToggleFilter", getToggleFilter},
        {"setToggleFilter", setToggleFilter}, {"updateFilter", browserNothing}, {"startBrowse", browserNothing},
        {"getScrollFadeSpeed", scrollFadeSpeed}, {"getHasNewBookmark", getHasNewBookmark},
        {"setHasNewBookmark", setHasNewBookmark}}},
      {"Singleplay", {}},
      {"Render", {}},
      {"Logic",
       {{"active", logicActive}, {"getModVersion", hostVersion}, {"setStorageString", storageSet},
        {"getStorageString", storageGet}, {"eraseStorageString", storageErase}, {"quit", quit},
        {"isGameRunning", isGameRunning}, {"getActivateReason", getActivateReason},
        {"setActivateReason", setActivateReason}, {"getFrameLabel", getFrameLabel},
        {"setFrameLabel", setFrameLabel}, {"isCapsLockPressed", isCapsLockPressed},
        {"setAsync", setAsync}, {"enableScroller", enableScroller}, {"getCommandline", absent},
        {"setDecimals", setDecimals}, {"getAsync", getAsync}, {"cacheCredits", cacheCredits},
        {"getIsLoading", logicIsLoading}, {"isFirstTimeLoading", logicFirstTimeLoading},
        {"readyToPlay", logicReadyToPlay}, {"isAbortLoadLevel", logicFalse}, {"isLoadingDemo", logicFalse},
        {"getNumRoundsLeft", numRoundsLeft}}},
      {"Locale",
       {{"getString", localeGetString}, {"loadString", localeGetString},
        {"getLanguage", localeLanguage}, {"checkXMLStatus", localeReady}}},
      {"MessageHandler", {}},
      {"General",
       {{"getListRevision", listRevision}, {"getListEntries", listEntries}, {"flushList", listRevision}}},
  };

  as_object* dice = object();
  as_object* bf2 = object();
  as_object* general = nullptr;
  for (const auto& [name, methods] : objects) {
    BridgeObject* o = makeObject(name, methods);
    global->set_member(name.c_str(), as_value(o));
    bf2->set_member(name.c_str(), as_value(o));
    if (name == "General") general = o;
  }
  // Settings objects live only under dice.bf2.
  bf2->set_member("GeneralSettings",
                  as_value(new SettingsObject("GeneralSettings", "GeneralSettings")));
  bf2->set_member("ServerSettings", as_value(new SettingsObject("ServerSettings", "GameServerSettings")));
  for (const char* name : {"GlobalSettings", "AudioSettings", "HapticSettings"}) {
    bf2->set_member(name, as_value(makeObject(name, {})));
  }
  bf2->set_member("VideoSettings",
                  as_value(makeObject("VideoSettings", {{"isRunningWithUnsupportedDrivers", unsupportedDrivers},
                                                        {"isOptimzingShaders", isOptimizingShaders}})));
  dice->set_member("bf2", as_value(bf2));
  // `General` also sits plainly in `dice`: lists call dice.General.getListEntries.
  if (general) dice->set_member("General", as_value(general));
  global->set_member("dice", as_value(dice));

  // `mx.lang.Locale` — the movie's own fallback class steps aside when it
  // exists (`if (_global.mx.lang.Locale)`).
  BridgeObject* locale = makeObject(
      "mx.lang.Locale",
      {{"addDelayedInstance", localeAddDelayed}, {"assignDelayedInstances", localeAssignDelayed},
       {"initialize", localeAssignDelayed}, {"start", localeAssignDelayed},
       {"checkXMLStatus", localeReady}, {"loadString", localeGetString},
       {"getString", localeGetString}, {"getLanguage", localeLanguage},
       {"setFlaName", absent}, {"setDefaultLang", absent}});
  as_object* lang = object();
  lang->set_member("Locale", as_value(locale));
  as_object* mx = object();
  mx->set_member("lang", as_value(lang));
  global->set_member("mx", as_value(mx));
}

}  // namespace obf2::swf
