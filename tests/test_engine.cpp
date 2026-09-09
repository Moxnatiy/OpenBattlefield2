#include <map>
#include <string>

#include "check.h"
#include "obf2/engine/console.h"
#include "obf2/engine/settings.h"

using namespace obf2;

namespace {

class MemoryFiles : public con::FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    return it == files.end() ? std::nullopt : std::optional<std::string>{it->second};
  }
};

// Runs .con text through the interpreter into the console — exactly as the engine
// does at startup.
void feed(engine::Console& console, const std::string& source) {
  MemoryFiles files;
  files.files["a.con"] = source;
  con::Interpreter interpreter(files,
                               [&](const con::Command& c) { console.execute(c); });
  interpreter.runFile("a.con");
}

}  // namespace

static void testDispatch() {
  engine::Console console;
  int calls = 0;
  std::string lastArgument;

  console.bind("game.setPlayerName", [&](const con::Command& c) {
    ++calls;
    lastArgument = std::string(c.argStr(0));
  });

  feed(console, "game.setPlayerName \"ARNE\"\n");
  CHECK_EQ(calls, 1);
  CHECK_EQ(lastArgument, std::string("ARNE"));
  CHECK_EQ(console.executedCount(), 1LL);
  CHECK_EQ(console.unknownCount(), 0LL);
}

static void testDispatchIsCaseInsensitive() {
  // In the game's files the same call occurs in different cases.
  engine::Console console;
  int calls = 0;
  console.bind("GeneralSettings.setViewIntroMovie", [&](const con::Command&) { ++calls; });

  feed(console,
       "GeneralSettings.setViewIntroMovie 1\n"
       "generalsettings.setviewintromovie 0\n"
       "GENERALSETTINGS.SETVIEWINTROMOVIE 1\n");
  CHECK_EQ(calls, 3);
}

static void testUnknownCommandsAreCounted() {
  // The port's main readiness metric: the engine knows 1735 commands, and we need
  // to see which of the ones that occurred are still without a handler.
  engine::Console console;
  console.bind("game.setPlayerName", [](const con::Command&) {});

  feed(console,
       "game.setPlayerName ARNE\n"
       "chat.setChatMessageSize 4\n"
       "chat.setChatMessageSize 5\n"
       "renderer.setSomethingNew 1\n");

  CHECK_EQ(console.executedCount(), 1LL);
  CHECK_EQ(console.unknownCount(), 3LL);
  CHECK_EQ(console.unknownCommands().size(), std::size_t(2));
  const auto found = console.unknownCommands().find("chat.setchatmessagesize");
  CHECK(found != console.unknownCommands().end());
  if (found != console.unknownCommands().end()) CHECK_EQ(found->second, 2);
}

static void testSettingsFromRealFileShape() {
  // The text is taken from Settings/VideoDefault.con and a stock game's profile.
  engine::Console console;
  engine::Settings settings;
  settings.bind(console);

  feed(console,
       "rem * TEMP *\n"
       "renderer.setFullScreen 1\n"
       "renderer.fieldOfView 1.2\n"
       "renderer.globalLodRadius 2\n"
       "renderer.setTerrainQuality 3\n"
       "renderer.setEffectsQuality 1\n"
       "renderer.setDynamicShadowsQuality 0\n"
       "renderer.setResolution 1280x1024@60\n"
       "game.setPlayerName \"ARNE\"\n"
       "game.setConnection 2\n"
       "game.setMinimapTransparency 20\n"
       "game.setCrossHairColor 255.000000 128.000000 0.000000\n"
       "GeneralSettings.setViewIntroMovie 0\n"
       "AudioSettings.setMusicVolume 0.35\n");

  CHECK(settings.video.fullScreen);
  CHECK(settings.video.fieldOfView > 1.19f && settings.video.fieldOfView < 1.21f);
  CHECK_EQ(settings.video.terrainQuality, 3);
  CHECK_EQ(settings.video.effectsQuality, 1);
  CHECK_EQ(settings.video.dynamicShadowsQuality, 0);
  CHECK_EQ(settings.video.width, 1280);
  CHECK_EQ(settings.video.height, 1024);

  CHECK_EQ(settings.general.playerName, std::string("ARNE"));
  CHECK_EQ(settings.general.connectionType, 2);
  CHECK_EQ(settings.general.minimapTransparency, 20);
  CHECK(!settings.general.viewIntroMovie);
  CHECK(settings.general.crosshairColor[1] > 127.9f && settings.general.crosshairColor[1] < 128.1f);

  CHECK(settings.audio.musicVolume > 0.34f && settings.audio.musicVolume < 0.36f);
}

static void testMissingArgumentKeepsPreviousValue() {
  // A corrupt line in the settings must not zero a value.
  engine::Console console;
  engine::Settings settings;
  settings.bind(console);
  settings.video.fieldOfView = 1.5f;

  feed(console, "renderer.fieldOfView\nrenderer.fieldOfView notANumber\n");
  CHECK(settings.video.fieldOfView > 1.49f && settings.video.fieldOfView < 1.51f);
}

// Aliases: an engine command; the game has 79 of them in Settings/AliasedCommands.con.
void testAliases() {
  engine::Console console;
  console.registerAliases();

  int calls = 0;
  console.bind("console.showfps", [&](const con::Command&) { ++calls; });

  // While there is no alias, the short name is unknown.
  CHECK(!console.executeLine("fps"));

  console.executeLine("alias fps console.showfps");
  CHECK_EQ(console.aliasCount(), std::size_t(1));
  CHECK(console.executeLine("fps"));
  CHECK_EQ(calls, 1);

  // The arguments reach the real command.
  std::string got;
  console.bind("game.setteam", [&](const con::Command& command) {
    got = std::string(command.argStr(0));
  });
  console.executeLine("alias team game.setTeam");
  console.executeLine("team 2");
  CHECK_EQ(got, std::string("2"));

  // A chain of aliases is expanded.
  console.executeLine("alias f fps");
  CHECK(console.executeLine("f"));
  CHECK_EQ(calls, 2);

  // A closed loop does not hang the console.
  console.executeLine("alias a b");
  console.executeLine("alias b a");
  CHECK(!console.executeLine("a"));
}

TEST_MAIN({
  testDispatch();
  testAliases();
  testDispatchIsCaseInsensitive();
  testUnknownCommandsAreCounted();
  testSettingsFromRealFileShape();
  testMissingArgumentKeepsPreviousValue();
})
