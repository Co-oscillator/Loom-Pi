#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 32
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL.h>

/* Font usage */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Turn off assert for performance (can turn on for debugging) */
#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0

/* Disable assembly optimizations which fail on macOS ARM64 */
#define LV_USE_NATIVE_HELIUM_ASM    0
#define LV_DRAW_SW_ASM_NONE         0
#define LV_USE_DRAW_SW_ASM          0

/* Optional built-in themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* Use standard C library allocator to prevent memory exhaustion crashes */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

#endif /*LV_CONF_H*/
