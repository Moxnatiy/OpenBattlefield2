// Єдина одиниця трансляції з реалізацією stb_image.
// Лишаємо тільки те, що справді трапляється в даних BF2: PNG (меню) і TGA
// (текстури інтерфейсу, напр. Ingame/Crosshair/ReferenceCross.tga). Решта
// декодерів вимкнена — кожен зайвий це зайва поверхня для помилок на
// недовірених даних.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "stb_image.h"
