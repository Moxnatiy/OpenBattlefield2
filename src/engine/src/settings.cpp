#include <cstdlib>
#include "obf2/engine/settings.h"

namespace obf2::engine {
namespace {

// Flags in a .con are written as 0/1, but true/false occurs too.
bool flagOf(const con::Command& command, bool fallback) {
  return command.argBool(0).value_or(fallback);
}

}  // namespace

void Settings::bind(Console& console) {
  // --- renderer.* : Settings/Video.con and VideoDefault.con ---
  console.bind("renderer.setFullScreen",
               [this](const con::Command& c) { video.fullScreen = flagOf(c, video.fullScreen); });
  console.bind("renderer.fullScreen",
               [this](const con::Command& c) { video.fullScreen = flagOf(c, video.fullScreen); });
  console.bind("renderer.fieldOfView", [this](const con::Command& c) {
    video.fieldOfView = c.argFloat(0).value_or(video.fieldOfView);
  });
  console.bind("renderer.globalLodRadius", [this](const con::Command& c) {
    video.globalLodRadius = c.argFloat(0).value_or(video.globalLodRadius);
  });
  console.bind("renderer.globalLodRadiusScaleFactor", [this](const con::Command& c) {
    video.globalLodRadiusScaleFactor = c.argFloat(0).value_or(video.globalLodRadiusScaleFactor);
  });
  console.bind("renderer.allowAllRefreshRates", [](const con::Command&) {});

  // The game keeps the resolution here as well: `game.setGameDisplayMode 800 600 32 0`
  // in the player's profile (Profiles/<profile>/Video.con). It is the same
  // resolution BF2 draws the HUD at, so we take it from here rather than
  // inventing our own.
  console.bind("game.setGameDisplayMode", [this](const con::Command& c) {
    const auto width = c.argInt(0);
    const auto height = c.argInt(1);
    if (width && height && *width > 0 && *height > 0) {
      video.width = *width;
      video.height = *height;
    }
    if (const auto fullScreen = c.argInt(3)) video.fullScreen = *fullScreen != 0;
  });

  // Quality levels: in the game these are integers 0..3 (low/medium/high/very
  // high), and the object they hang off is `VideoSettings` — that is the name
  // `BF2.exe` registers (`setTerrainQuality` at 0x00413d29, an `int`
  // argument) and the one a profile's `VideoSettings.setResolution` is written
  // with. We had bound them to `renderer.*`, which is in neither binary; the
  // `game.set*` aliases further down did the work by accident. Both names are
  // bound now. What each of the ten changes in the original is
  // docs/TODO-graphics.md.
  console.bind("VideoSettings.setTerrainQuality", [this](const con::Command& c) {
    video.terrainQuality = c.argInt(0).value_or(video.terrainQuality);
  });
  console.bind("VideoSettings.setEffectsQuality", [this](const con::Command& c) {
    video.effectsQuality = c.argInt(0).value_or(video.effectsQuality);
  });
  console.bind("VideoSettings.setGeometryQuality", [this](const con::Command& c) {
    video.geometryQuality = c.argInt(0).value_or(video.geometryQuality);
  });
  console.bind("VideoSettings.setTextureQuality", [this](const con::Command& c) {
    video.textureQuality = c.argInt(0).value_or(video.textureQuality);
  });
  console.bind("VideoSettings.setLightingQuality", [this](const con::Command& c) {
    video.lightingQuality = c.argInt(0).value_or(video.lightingQuality);
  });
  console.bind("VideoSettings.setDynamicShadowsQuality", [this](const con::Command& c) {
    video.dynamicShadowsQuality = c.argInt(0).value_or(video.dynamicShadowsQuality);
  });
  console.bind("VideoSettings.setDynamicLightingQuality", [this](const con::Command& c) {
    video.dynamicLightingQuality = c.argInt(0).value_or(video.dynamicLightingQuality);
  });
  console.bind("VideoSettings.setAntialiasing", [this](const con::Command& c) {
    video.antialiasing = c.argInt(0).value_or(video.antialiasing);
  });
  // The two the struct did not carry at all. Texture filtering becomes the
  // `FILTER_*` defines the engine pastes into every sampler declaration
  // (`RendDX9.dll` holds the text: `#define FILTER_STM_DIFF_MIN %s`), and the
  // view distance is a **float** scale, not a level.
  console.bind("VideoSettings.setTextureFilteringQuality", [this](const con::Command& c) {
    video.textureFilteringQuality = c.argInt(0).value_or(video.textureFilteringQuality);
  });
  console.bind("VideoSettings.setViewDistanceScale", [this](const con::Command& c) {
    video.viewDistanceScale = c.argFloat(0).value_or(video.viewDistanceScale);
  });
  console.bind("VideoSettings.setResolution", [this](const con::Command& c) {
    // The format "1280x1024@60" — we take only the size.
    const std::string_view value = c.argStr(0);
    const std::size_t cross = value.find('x');
    if (cross == std::string_view::npos) return;
    video.width = std::atoi(std::string(value.substr(0, cross)).c_str());
    const std::size_t at = value.find('@', cross);
    const std::string_view heightText =
        value.substr(cross + 1, at == std::string_view::npos ? at : at - cross - 1);
    video.height = std::atoi(std::string(heightText).c_str());
  });

  // --- game.* : Settings/Profiles/<profile>/GeneralOptions.con ---
  console.bind("game.setPlayerName",
               [this](const con::Command& c) { general.playerName = std::string(c.argStr(0)); });
  console.bind("game.setConnection", [this](const con::Command& c) {
    general.connectionType = c.argInt(0).value_or(general.connectionType);
  });
  console.bind("game.setMinimapTransparency", [this](const con::Command& c) {
    general.minimapTransparency = c.argInt(0).value_or(general.minimapTransparency);
  });
  console.bind("game.setStaticMinimap", [this](const con::Command& c) {
    general.staticMinimap = flagOf(c, general.staticMinimap);
  });
  console.bind("game.setToolTip",
               [this](const con::Command& c) { general.toolTip = flagOf(c, general.toolTip); });
  console.bind("game.setCrossHairColor", [this](const con::Command& c) {
    for (std::size_t i = 0; i < 3; ++i) {
      general.crosshairColor[i] = c.argFloat(i).value_or(general.crosshairColor[i]);
    }
  });

  // --- GeneralSettings.* : the same, but under the binary's names ---
  console.bind("GeneralSettings.setViewIntroMovie", [this](const con::Command& c) {
    general.viewIntroMovie = flagOf(c, general.viewIntroMovie);
  });
  console.bind("GeneralSettings.setHUDTransparency", [this](const con::Command& c) {
    general.hudTransparency = c.argInt(0).value_or(general.hudTransparency);
  });
  console.bind("GeneralSettings.setMinimapTransparency", [this](const con::Command& c) {
    general.minimapTransparency = c.argInt(0).value_or(general.minimapTransparency);
  });
  console.bind("GeneralSettings.setConnectionType", [this](const con::Command& c) {
    general.connectionType = c.argInt(0).value_or(general.connectionType);
  });

  // --- game.*: the rest of the profile ---
  console.bind("game.setRadioToolTip", [this](const con::Command& c) {
    general.radioToolTip = flagOf(c, general.radioToolTip);
  });
  console.bind("game.setSkirmishPercentageOfBots", [this](const con::Command& c) {
    general.skirmishPercentageOfBots = c.argInt(0).value_or(general.skirmishPercentageOfBots);
  });
  console.bind("game.setSkirmishPercentageOfCpu", [this](const con::Command& c) {
    general.skirmishPercentageOfCpu = c.argInt(0).value_or(general.skirmishPercentageOfCpu);
  });
  console.bind("game.setCampaignPercentageOfBots", [this](const con::Command& c) {
    general.campaignPercentageOfBots = c.argInt(0).value_or(general.campaignPercentageOfBots);
  });
  console.bind("game.setCampaignPercentageOfCpu", [this](const con::Command& c) {
    general.campaignPercentageOfCpu = c.argInt(0).value_or(general.campaignPercentageOfCpu);
  });
  console.bind("game.setDefaultIp", [this](const con::Command& c) {
    general.defaultIp = std::string(c.argStr(0));
    general.defaultPort = c.argInt(1).value_or(general.defaultPort);
  });
  console.bind("game.setRadioToolTipColor", [](const con::Command&) {});

  // The profile also sets the graphics quality through game.* — the same values
  // as renderer.set*Quality, just by another route.
  console.bind("game.setGraphicsQuality", [this](const con::Command& c) {
    video.geometryQuality = c.argInt(0).value_or(video.geometryQuality);
  });
  console.bind("game.setEffectsQuality", [this](const con::Command& c) {
    video.effectsQuality = c.argInt(0).value_or(video.effectsQuality);
  });
  console.bind("game.setTerrainQuality", [this](const con::Command& c) {
    video.terrainQuality = c.argInt(0).value_or(video.terrainQuality);
  });
  console.bind("game.setGameDisplayMode", [this](const con::Command& c) {
    video.fullScreen = c.argInt(0).value_or(0) != 0;
  });
  console.bind("game.setDetailTexture", [](const con::Command&) {});
  console.bind("game.setLightmaps", [](const con::Command&) {});
  console.bind("game.setShadows", [this](const con::Command& c) {
    video.dynamicShadowsQuality = c.argInt(0).value_or(video.dynamicShadowsQuality);
  });
  console.bind("game.setPerformance", [](const con::Command&) {});
  console.bind("game.setMenuViewDistance", [](const con::Command&) {});
  console.bind("game.setRenderWhenSpawnMenu", [](const con::Command&) {});
  console.bind("game.setTextureQuality", [this](const con::Command& c) {
    video.textureQuality = c.argInt(0).value_or(video.textureQuality);
  });
  console.bind("game.setLightingQuality", [this](const con::Command& c) {
    video.lightingQuality = c.argInt(0).value_or(video.lightingQuality);
  });
  console.bind("game.setAntialiasing", [this](const con::Command& c) {
    video.antialiasing = c.argInt(0).value_or(video.antialiasing);
  });
  console.bind("game.setEnvironmentMapping", [](const con::Command&) {});
  console.bind("game.setViewDistanceScale", [this](const con::Command& c) {
    video.globalLodRadius = c.argFloat(0).value_or(video.globalLodRadius);
  });

  // --- chat.* ---
  console.bind("chat.setChatMessageSize", [this](const con::Command& c) {
    chat.chatMessageSize = c.argInt(0).value_or(chat.chatMessageSize);
  });
  console.bind("chat.setGameInfoMessageSize", [this](const con::Command& c) {
    chat.gameInfoMessageSize = c.argInt(0).value_or(chat.gameInfoMessageSize);
  });
  console.bind("chat.setKillMessageSize", [this](const con::Command& c) {
    chat.killMessageSize = c.argInt(0).value_or(chat.killMessageSize);
  });
  console.bind("chat.setOldChatListStyle", [this](const con::Command& c) {
    chat.oldChatListStyle = c.argInt(0).value_or(chat.oldChatListStyle);
  });
  console.bind("chat.setOldChatListHistory", [this](const con::Command& c) {
    chat.oldChatListHistory = c.argInt(0).value_or(chat.oldChatListHistory);
  });
  console.bind("chat.setTimeUntilMessageRemoved", [this](const con::Command& c) {
    chat.timeUntilMessageRemoved = c.argFloat(0).value_or(chat.timeUntilMessageRemoved);
  });
  console.bind("chat.setIgnoreRadioText", [this](const con::Command& c) {
    chat.ignoreRadioText = flagOf(c, chat.ignoreRadioText);
  });
  console.bind("chat.setIgnoreRadioAudio", [this](const con::Command& c) {
    chat.ignoreRadioAudio = flagOf(c, chat.ignoreRadioAudio);
  });

  // --- the console ---
  console.bind("console.showStats", [](const con::Command&) {});
  console.bind("console.showFps", [](const con::Command&) {});

  // --- sound ---
  console.bind("AudioSettings.setEffectsVolume", [this](const con::Command& c) {
    audio.effectsVolume = c.argFloat(0).value_or(audio.effectsVolume);
  });
  console.bind("AudioSettings.setMusicVolume", [this](const con::Command& c) {
    audio.musicVolume = c.argFloat(0).value_or(audio.musicVolume);
  });
  console.bind("AudioSettings.setHelpVoiceVolume", [this](const con::Command& c) {
    audio.helpVoiceVolume = c.argFloat(0).value_or(audio.helpVoiceVolume);
  });
  console.bind("AudioSettings.setEnableEAX",
               [this](const con::Command& c) { audio.enableEax = flagOf(c, audio.enableEax); });
  console.bind("sound.setSoundEffectsVolume", [this](const con::Command& c) {
    audio.effectsVolume = c.argFloat(0).value_or(audio.effectsVolume);
  });
  console.bind("sound.setMusicVolume", [this](const con::Command& c) {
    audio.musicVolume = c.argFloat(0).value_or(audio.musicVolume);
  });
}

}  // namespace obf2::engine
