// Єдина одиниця трансляції з реалізацією stb_image.
// Вимикаємо все, крім PNG: інші формати нам не потрібні, а кожен зайвий
// декодер — це зайва поверхня для помилок на недовірених даних.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
