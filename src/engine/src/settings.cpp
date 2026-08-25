#include <cstdlib>
#include "obf2/engine/settings.h"

namespace obf2::engine {
namespace {

// Прапорці в .con записані як 0/1, але трапляється і true/false.
bool flagOf(const con::Command& command, bool fallback) {
  return command.argBool(0).value_or(fallback);
}

}  // namespace

void Settings::bind(Console& console) {
  // --- renderer.* : Settings/Video.con і VideoDefault.con ---
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

  // Рівні якості: у грі це цілі 0..3 (низька/середня/висока/дуже висока).
  console.bind("renderer.setTerrainQuality", [this](const con::Command& c) {
    video.terrainQuality = c.argInt(0).value_or(video.terrainQuality);
  });
  console.bind("renderer.setEffectsQuality", [this](const con::Command& c) {
    video.effectsQuality = c.argInt(0).value_or(video.effectsQuality);
  });
  console.bind("renderer.setGeometryQuality", [this](const con::Command& c) {
    video.geometryQuality = c.argInt(0).value_or(video.geometryQuality);
  });
  console.bind("renderer.setTextureQuality", [this](const con::Command& c) {
    video.textureQuality = c.argInt(0).value_or(video.textureQuality);
  });
  console.bind("renderer.setLightingQuality", [this](const con::Command& c) {
    video.lightingQuality = c.argInt(0).value_or(video.lightingQuality);
  });
  console.bind("renderer.setDynamicShadowsQuality", [this](const con::Command& c) {
    video.dynamicShadowsQuality = c.argInt(0).value_or(video.dynamicShadowsQuality);
  });
  console.bind("renderer.setDynamicLightingQuality", [this](const con::Command& c) {
    video.dynamicLightingQuality = c.argInt(0).value_or(video.dynamicLightingQuality);
  });
  console.bind("renderer.setAntialiasing", [this](const con::Command& c) {
    video.antialiasing = c.argInt(0).value_or(video.antialiasing);
  });
  console.bind("renderer.setResolution", [this](const con::Command& c) {
    // Формат "1280x1024@60" — беремо лише розмір.
    const std::string_view value = c.argStr(0);
    const std::size_t cross = value.find('x');
    if (cross == std::string_view::npos) return;
    video.width = std::atoi(std::string(value.substr(0, cross)).c_str());
    const std::size_t at = value.find('@', cross);
    const std::string_view heightText =
        value.substr(cross + 1, at == std::string_view::npos ? at : at - cross - 1);
    video.height = std::atoi(std::string(heightText).c_str());
  });

  // --- game.* : Settings/Profiles/<профіль>/GeneralOptions.con ---
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

  // --- GeneralSettings.* : те саме, але іменами з бінаря ---
  console.bind("GeneralSettings.setViewIntroMovie", [this](const con::Command& c) {
    general.viewIntroMovie = flagOf(c, general.viewIntroMovie);
  });
  console.bind("GeneralSettings.setMinimapTransparency", [this](const con::Command& c) {
    general.minimapTransparency = c.argInt(0).value_or(general.minimapTransparency);
  });
  console.bind("GeneralSettings.setConnectionType", [this](const con::Command& c) {
    general.connectionType = c.argInt(0).value_or(general.connectionType);
  });

  // --- game.*: решта профілю ---
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

  // Якість графіки профіль задає ще й через game.* — ті самі значення,
  // що й renderer.set*Quality, лише іншим шляхом.
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

  // --- консоль ---
  console.bind("console.showStats", [](const con::Command&) {});
  console.bind("console.showFps", [](const con::Command&) {});

  // --- звук ---
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
