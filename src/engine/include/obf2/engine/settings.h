#pragma once
// Налаштування гри.
//
// Імена команд узяті 1:1 з оригіналу — і з файлів гри (`Settings/*.con`,
// `Settings/Profiles/<профіль>/*.con`), і з таблиці рядків BF2.exe. Це той
// випадок, коли відхилятися не можна взагалі: файли налаштувань пише сам
// користувач, і вони мають лишатися сумісними.
//
// Значення за замовчуванням — ті, що в стоковому BF2.
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
  // Прозорість HUD і мінікарти. У грі це байт 0..255, а вузли HUD
  // дістають його вже часткою: BF2.exe множить обидва поля на 1/255
  // (0x8a4a64) і кладе у змінні MenuBackgroundAlpha та MenuMapAlpha
  // (див. 0x4b68d3 і 0x4b6907). Типове значення профілю — 204.
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

// Розміри повідомлень у чаті — окремий блок команд `chat.*`.
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

  // Реєструє обробники в консолі. Все, що прочитається з .con, потрапить
  // сюди; решта осяде в списку невідомих команд.
  void bind(Console& console);
};

}  // namespace obf2::engine
