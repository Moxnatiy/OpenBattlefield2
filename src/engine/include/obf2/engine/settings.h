#pragma once
// The game's settings.
//
// The command names are taken 1:1 from the original — both from the game's files
// (`Settings/*.con`, `Settings/Profiles/<profile>/*.con`) and from BF2.exe's
// string table. This is the case where departing is not allowed at all: the
// settings files are written by the user, and they have to stay compatible.
//
// The default values are the ones in a stock BF2.
#include <string>

#include "obf2/engine/console.h"

namespace obf2::engine {

struct VideoSettings {
  bool fullScreen = false;
  int width = 1280;
  int height = 720;
  float fieldOfView = 1.0f;
  float globalLodRadius = 1.0f;
  float globalLodRadiusScaleFactor = 5.0f;
  int terrainQuality = 2;
  int effectsQuality = 2;
  int geometryQuality = 2;
  int textureQuality = 2;
  int lightingQuality = 2;
  int dynamicShadowsQuality = 2;
  int dynamicLightingQuality = 2;
  int antialiasing = 0;
};

struct GeneralSettings {
  std::string playerName;
  bool viewIntroMovie = true;   // GeneralSettings.setViewIntroMovie
  int connectionType = 2;       // game.setConnection
  // The HUD's and the minimap's alpha. In the game it is a byte 0..255, while
  // the HUD's nodes receive it as a fraction: BF2.exe multiplies both fields by
  // 1/255 (0x8a4a64) and puts them into the variables MenuBackgroundAlpha and
  // MenuMapAlpha (see 0x4b68d3 and 0x4b6907). The profile's default is 204.
  int hudTransparency = 204;      // GeneralSettings.setHUDTransparency
  int minimapTransparency = 204;  // GeneralSettings.setMinimapTransparency
  bool staticMinimap = true;
  bool toolTip = false;
  bool radioToolTip = true;
  float crosshairColor[3] = {255.0f, 255.0f, 0.0f};

  int skirmishPercentageOfBots = 150;
  int skirmishPercentageOfCpu = 20;
  int campaignPercentageOfBots = 150;
  int campaignPercentageOfCpu = 20;
  std::string defaultIp;
  int defaultPort = 0;
};

// The chat messages' sizes — a separate block of `chat.*` commands.
struct ChatSettings {
  int chatMessageSize = 4;
  int gameInfoMessageSize = 2;
  int killMessageSize = 3;
  int oldChatListStyle = 0;
  int oldChatListHistory = 8;
  float timeUntilMessageRemoved = 10.0f;
  bool ignoreRadioText = false;
  bool ignoreRadioAudio = false;
};

struct AudioSettings {
  float effectsVolume = 1.0f;
  float musicVolume = 1.0f;
  float helpVoiceVolume = 1.0f;
  bool enableEax = false;
};

struct Settings {
  VideoSettings video;
  GeneralSettings general;
  AudioSettings audio;
  ChatSettings chat;

  // Registers the handlers in the console. Everything that reads out of a .con
  // lands here; the rest settles in the list of unknown commands.
  void bind(Console& console);
};

}  // namespace obf2::engine
