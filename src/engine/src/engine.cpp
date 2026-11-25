#include "obf2/engine/engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>

#include "obf2/core/path.h"
#include "obf2/engine/bink.h"

namespace obf2::engine {
namespace {

std::string lowered(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// The mod's settings, read before the profile (docs/functions/startup.md):
// `settingsManager->load("Settings/Usersettings.con")` after the archives are
// mounted (B2BF's `BF2Engine::initEngine`; the call **not checked** in BF2.exe).
constexpr const char* kUserSettingsFile = "Settings/Usersettings.con";

// Read by `BF2Engine::init` in this order, all three pushed in one function:
// 0x40c6c2, 0x40c7fb, 0x40c934 (`BF2.exe`). Init.con queues the intro movies.
constexpr const char* kEngineInitFiles[] = {
    "Settings/Sound.con",
    "Menu/MenuSounds.con",
    "Init.con",
};

// Not found being read by anything: no binary, script or movie names them. They are
// kept because the port needs what is in them — the 79 console aliases and the
// default key bindings (the original's default control maps are built in code,
// `defaultPlayerInputControlMap` 0x40c5d5) — and printed apart from the engine's own.
constexpr const char* kUnattestedFiles[] = {
    "Settings/Controls.con",
    "Settings/AliasedCommands.con",
};


// A minimal extraction of a tag's content. A .desc is XML, but all we need from
// it is <name>, and dragging in a full parser for one tag is pointless.
std::string tagValue(std::string_view xml, std::string_view tag) {
  const std::string open = "<" + std::string(tag) + ">";
  const std::string close = "</" + std::string(tag) + ">";
  const std::size_t start = xml.find(open);
  if (start == std::string_view::npos) return {};
  const std::size_t from = start + open.size();
  const std::size_t end = xml.find(close, from);
  if (end == std::string_view::npos) return {};

  std::string_view value = xml.substr(from, end - from);
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  return std::string(value);
}

// An attribute's value: <briefing locid="LOADINGSCREEN_MAPDESCRIPTION_dalianplant">
std::string attributeValue(std::string_view xml, std::string_view tag, std::string_view attribute) {
  const std::size_t tagStart = xml.find("<" + std::string(tag));
  if (tagStart == std::string_view::npos) return {};
  const std::size_t tagEnd = xml.find('>', tagStart);
  if (tagEnd == std::string_view::npos) return {};

  const std::string_view inside = xml.substr(tagStart, tagEnd - tagStart);
  const std::string needle = std::string(attribute) + "=\"";
  const std::size_t at = inside.find(needle);
  if (at == std::string_view::npos) return {};

  const std::size_t from = at + needle.size();
  const std::size_t quote = inside.find('"', from);
  if (quote == std::string_view::npos) return {};
  return std::string(inside.substr(from, quote - from));
}

}  // namespace

std::string_view stateName(State state) {
  switch (state) {
    case State::Boot: return "Boot";
    case State::Intro: return "Intro";
    case State::MainMenu: return "MainMenu";
    case State::Loading: return "Loading";
    case State::InGame: return "InGame";
  }
  return "?";
}

bool Engine::boot(FileSystem& files, const std::filesystem::path& modDir,
                  const std::filesystem::path& homeDir) {
  settings_.bind(console_);
  controls_.bind(console_);
  console_.registerAliases();
  modDir_ = modDir;

  // The credits' commands (credits.con; the names are BF2.exe's strings).
  console_.bind("swiffHost.setScrollFadeSpeed", [this](const con::Command& command) {
    credits_.fadeSpeed = command.argInt(0).value_or(0);
  });
  console_.bind("swiffHost.setScrollSpeed", [this](const con::Command& command) {
    credits_.scrollSpeed = command.argInt(0).value_or(0);
  });
  console_.bind("swiffHost.setScrollerRect", [this](const con::Command& command) {
    for (int i = 0; i < 4; ++i) credits_.rect[i] = command.argInt(static_cast<std::size_t>(i)).value_or(0);
  });
  console_.bind("swiffHost.addCredit", [this](const con::Command& command) {
    credits_.lines.emplace_back(std::string(command.argStr(0)), command.argInt(1).value_or(0));
  });

  // `bf2Engine.playMovie <name> <flag>` (0x407a60) fills the queue; Init.con calls it.
  console_.bind("bf2Engine.playMovie", [this](const con::Command& command) {
    movies_.play(std::string(command.argStr(0)), command.argInt(1).value_or(0));
  });

  // The settings are read with the same interpreter as everything else: in
  // Refractor 2 the console is the single entry point for any .con.
  con::Interpreter interpreter(
      files, [this](const con::Command& command) { console_.execute(command); });
  const auto run = [&](const char* name) {
    if (!files.exists(name)) return;
    bootFiles_.emplace_back(name);
    interpreter.runFile(name);
  };

  run(kUserSettingsFile);
  for (const char* name : kUnattestedFiles) run(name);
  loadDefaultProfile(homeDir);
  for (const char* name : kEngineInitFiles) run(name);

  loadLexicon(files);
  scanLevels(files, modDir);

  // The first movie starts on the main loop's first pass (0x40cd63).
  movies_.check([this](const std::string& name) { return openMovie(name); });
  enter(movies_.playing() ? State::Intro : State::MainMenu);
  return true;
}

void Engine::loadDefaultProfile(const std::filesystem::path& homeDir) {
  // `LocalProfileManager::init` (`BF2.exe` 0x4a8980) makes a profile named
  // "Default" (0x4a89b6), loads it (`loadCurrentProfile`, slot +0x3C = 0x4ab1d0)
  // and applies its sound (`applySettings(true)`, +0x58). The named profile the
  // player logs in with (`Global.con`'s default user) is loaded only at login.
  // When this runs against `BF2Engine::init`'s scripts is not established.
  homeDir_ = homeDir;
  console_.bind("GlobalSettings.setDefaultUser", [this](const con::Command& command) {
    defaultUser_ = std::string(command.argStr(0));
  });
  console_.bind("GlobalSettings.setNamePrefix", [this](const con::Command& command) {
    namePrefix_ = std::string(command.argStr(0));
  });
  // The settings objects the menu's bridge reaches by name — GameServerSettings
  // (ServerSettings, SwiffPlayer.dll 0x10066760), GeneralSettings
  // (0x10033b50; setMaxBots 0x10031f40 → the object's +0xb4) — keep every
  // `set<Name> value` they are given, from the profile's .con or the menu.
  // GeneralSettings' few fields the engine itself uses keep their own
  // handlers too (settings.cpp). Several arguments are kept joined by spaces.
  for (const char* object : {"GameServerSettings", "GeneralSettings"}) {
    const std::string owner = object;
    console_.observe(owner, [this, owner](const con::Command& command) {
      const std::string_view method = command.method();
      if (method.size() <= 3 || lowered(std::string(method.substr(0, 3))) != "set") return;
      std::string value;
      for (std::size_t i = 0; i < command.args.size(); ++i) {
        if (i) value += ' ';
        value += command.args[i];
      }
      setSetting(owner, std::string(method.substr(3)), value);
    });
  }
  if (homeDir.empty()) {
    std::printf("profile: no @HOME@ folder, the profiles are not read\n");
    return;
  }
  loadProfile("Default");
}

std::vector<Engine::LocalProfile> Engine::localProfiles() const {
  // `getLocalProfiles` lists the numbered folders of `getProfilesPath`
  // (0x4a76a0); each one's names come from its Profile.con, read here with
  // the same interpreter into a console of its own so the engine's settings
  // are not touched.
  std::vector<LocalProfile> out;
  std::error_code ec;
  const std::filesystem::path root = homeDir_ / "Profiles";
  if (homeDir_.empty() || !std::filesystem::is_directory(root, ec)) return out;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_directory()) continue;
    const std::string id = entry.path().filename().string();
    if (id.empty() || !std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; })) {
      continue;
    }
    LocalProfile profile;
    profile.id = id;
    FileSystem files;
    files.mountDirectory(entry.path());
    Console console;
    console.bind("LocalProfile.setName",
                 [&](const con::Command& c) { profile.name = std::string(c.argStr(0)); });
    console.bind("LocalProfile.setNick",
                 [&](const con::Command& c) { profile.nick = std::string(c.argStr(0)); });
    console.bind("LocalProfile.setGamespyNick",
                 [&](const con::Command& c) { profile.gamespyNick = std::string(c.argStr(0)); });
    con::Interpreter interpreter(files, [&](const con::Command& c) { console.execute(c); });
    if (files.exists("Profile.con")) interpreter.runFile("Profile.con");
    out.push_back(std::move(profile));
  }
  std::sort(out.begin(), out.end(),
            [](const LocalProfile& a, const LocalProfile& b) { return a.id < b.id; });
  return out;
}

bool Engine::login(const std::string& profile) {
  const bool loaded = loadProfile(profile);
  std::printf("profile: login %s — %s\n", profile.c_str(), loaded ? "loaded" : "failed");
  return loaded;
}

bool Engine::loadProfile(const std::string& profile) {
  // `getProfilesPath` 0x4a76a0: "@HOME@/Profiles/".
  FileSystem profiles;
  if (homeDir_.empty() || !profiles.mountDirectory(homeDir_ / "Profiles")) return false;
  con::Interpreter interpreter(
      profiles, [this](const con::Command& command) { console_.execute(command); });
  const auto run = [&](const std::string& name) {
    if (!profiles.exists(name)) return false;
    bootFiles_.emplace_back("@HOME@/Profiles/" + name);
    return interpreter.runFile(name);
  };
  const std::string folder = profile + "/";

  // `loadCurrentProfile` 0x4ab1d0. Both branches start with the global settings'
  // file (0x4ab23b: the global settings at 0x987380, slot +0x10, "Global.con"
  // 0x8a34c4), then branch on the profile being named "Default" (0x4ab200).
  run("Global.con");

  if (profile == "Default") {
    // 0x4ab25c / 0x4ab267: the audio (0x987384 +0x24) and video (0x987388 +0x20)
    // settings' defaults — ours are the structures' own initial values.
    // 0x4ab285: Profile.con, failing that "unable to open current profile".
    if (!run(folder + "Profile.con")) {
      std::printf("profile: unable to open current profile (%s)\n", profile.c_str());
      return false;
    }
    run(folder + "Audio.con");  // 0x4ab30c
    run(folder + "Video.con");  // 0x4ab361
    currentProfile_ = profile;
    return true;
  }

  // The named branch: 0x4ab4ae Profile.con, failing that the same message.
  if (!run(folder + "Profile.con")) {
    std::printf("profile: unable to open current profile (%s)\n", profile.c_str());
    return false;
  }
  // 0x4ab522: "Default/ServerSettings.con" becomes the server settings' config
  // file name (+0x4c) — a name kept, not a file run.
  run(folder + "General.con");         // 0x4ab587, general settings 0x987390
  run(folder + "ServerSettings.con");  // 0x4ab5dc, game server settings 0x987354
  run(folder + "Audio.con");           // 0x4ab631, 0x987384
  run(folder + "Video.con");           // 0x4ab686, 0x987388
  run(folder + "Haptic.con");          // 0x4ab6db, 0x987394
  // 0x4ab716..0x4ab749: the input manager's control maps reset first (0x9ffcb4
  // +0x34 twice, +0x14; the control settings +0x28) — not modelled.
  run(folder + "Controls.con");        // 0x4ab774, control settings 0x98738c
  // 0x4ab80b DemoBookmarks.con, 0x4ab86d the map list; 0x4ab8a0 the profile's
  // name into the setting GSPlayerName — not modelled.
  currentProfile_ = profile;
  return true;
}

std::optional<double> Engine::openMovie(const std::string& name) const {
  // The movies lie loose in the mod's directory, not in an archive, and they are
  // big (Intro.bik is 138 MB), so only the head is read, from disk. Whether the
  // original's player opens them through the file manager or straight from disk
  // is not established; a case-sensitive volume would miss `dice.bik` (the file is
  // `Dice.bik`).
  const std::filesystem::path path = modDir_ / normalizeAssetPath(name);
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::array<std::byte, kBinkHeaderBytes> head{};
  in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  if (in.gcount() != static_cast<std::streamsize>(head.size())) return std::nullopt;
  const auto header = readBinkHeader(head);
  if (!header) return std::nullopt;
  return header->seconds();
}

void Engine::loadLexicon(FileSystem& files) {
  // The game reads several files per language: the main one, patches, add-ons.
  // Later ones override earlier, so the order matters.
  for (const char* name : {"Localization/English/english.utxt",
                           "Localization/English/English_Patch.utxt",
                           "Localization/English/XPEnglish.utxt"}) {
    const auto bytes = files.read(name);
    if (bytes) lexicon_.addUtxt(*bytes);
  }
  // The client reads every file of `localization/<language>/` (initLocalization,
  // BF2.exe 0x4086e0: the path built at 0x40879f from "localization/" and the
  // language, "English" by default at 0x408714, then a folder walk). The rest of
  // the folder fills in the keys the three above lack — English_Falcon.utxt's
  // OPTIONS_6 among them, the options' FALCON tab.
  std::vector<std::string> folder = files.list("Localization/English/");
  std::sort(folder.begin(), folder.end());  // the walk's own order is not read
  for (const std::string& path : folder) {
    if (path.size() < 5 || path.compare(path.size() - 5, 5, ".utxt") != 0) continue;
    if (const auto bytes = files.read(path)) lexicon_.addUtxtMissing(*bytes);
  }
}

void Engine::scanLevels(FileSystem& files, const std::filesystem::path& modDir) {
  std::error_code ec;
  const std::filesystem::path levelsDir = modDir / "Levels";
  if (!std::filesystem::is_directory(levelsDir, ec)) return;

  for (const auto& entry : std::filesystem::directory_iterator(levelsDir, ec)) {
    if (ec) break;
    if (!entry.is_directory()) continue;

    LevelEntry level;
    level.directory = entry.path().filename().string();
    level.displayName = level.directory;

    // The name lies in Info/<name>.desc — the same place as the loading picture.
    const std::string base = "Levels/" + level.directory + "/Info/";
    if (const auto desc = files.read(base + level.directory + ".desc")) {
      const std::string xml(reinterpret_cast<const char*>(desc->data()), desc->size());
      const std::string name = tagValue(xml, "name");
      if (!name.empty()) level.displayName = name;
      level.briefingKey = attributeValue(xml, "briefing", "locid");
      level.music = tagValue(xml, "music");  // BF2.exe 0x4802fe
    }
    if (files.exists(base + "loadmap.png")) level.loadImage = base + "loadmap.png";

    levels_.push_back(std::move(level));
  }

  std::sort(levels_.begin(), levels_.end(),
            [](const LevelEntry& a, const LevelEntry& b) { return a.displayName < b.displayName; });
}

std::string readModMusic(FileSystem& files) {
  const auto descriptor = files.loadText("mod.desc");  // BF2.exe 0x47ddab
  return descriptor ? tagValue(*descriptor, "music") : std::string{};  // 0x47df11
}

bool Engine::startLoading(std::string_view levelDirectory) {
  for (std::size_t i = 0; i < levels_.size(); ++i) {
    if (levels_[i].directory == levelDirectory) {
      loadingIndex_ = static_cast<int>(i);
      enter(State::Loading);
      return true;
    }
  }
  return false;
}

void Engine::finishLoading() {
  if (state_ == State::Loading) enter(State::InGame);
}

const LevelEntry* Engine::loadingLevel() const {
  if (loadingIndex_ < 0 || loadingIndex_ >= static_cast<int>(levels_.size())) return nullptr;
  return &levels_[static_cast<std::size_t>(loadingIndex_)];
}

void Engine::enter(State next) { state_ = next; }

void Engine::skipAllMovies() {
  movies_.clear();
  if (state_ == State::Intro) enter(State::MainMenu);
}

void Engine::update(float deltaSeconds, bool escapePressed) {
  if (state_ != State::Intro) return;
  movies_.frame(deltaSeconds, escapePressed,
                [this](const std::string& name) { return openMovie(name); });
  if (!movies_.playing()) enter(State::MainMenu);
}

void Engine::cacheCredits(FileSystem& files) {
  credits_.lines.clear();
  con::Interpreter interpreter(files, [this](const con::Command& command) { console_.execute(command); });
  interpreter.runFile("menu/external/flashmenu/credits.con");
}

std::string Engine::setting(const std::string& object, const std::string& name) const {
  const auto found = objectSettings_.find(lowered(object + "." + name));
  return found == objectSettings_.end() ? std::string() : found->second;
}

void Engine::setSetting(const std::string& object, const std::string& name, const std::string& value) {
  objectSettings_[lowered(object + "." + name)] = value;
}

}  // namespace obf2::engine
