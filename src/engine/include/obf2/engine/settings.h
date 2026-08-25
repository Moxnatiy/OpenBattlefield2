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
  int minimapTransparency = 20;
  bool staticMinimap = true;
  bool toolTip = false;
  float crosshairColor[3] = {255.0f, 255.0f, 0.0f};
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

  // Реєструє обробники в консолі. Все, що прочитається з .con, потрапить
  // сюди; решта осяде в списку невідомих команд.
  void bind(Console& console);
};

}  // namespace obf2::engine
