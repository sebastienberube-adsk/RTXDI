// Provides stb_image / stb_image_write implementations for standalone test
// executables that don't link against donut_engine (which has its own copy).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
