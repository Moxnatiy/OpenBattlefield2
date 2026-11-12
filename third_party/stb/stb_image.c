// The single translation unit with stb_image's implementation.
// We keep only what really occurs in BF2's data: PNG (the menu), TGA
// (interface textures, e.g. Ingame/Crosshair/ReferenceCross.tga) and JPEG (the
// pictures inside the menu's own movie, DefineBits/DefineBitsJPEG2, which our
// Flash player decodes — third_party/gameswf). The other decoders are disabled —
// every extra one is extra attack surface on untrusted data.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"
