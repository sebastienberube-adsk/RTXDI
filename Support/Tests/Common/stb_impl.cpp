// Provides stb_image implementation for standalone test executables
// that don't link against donut_engine (which has its own stb_image impl).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
