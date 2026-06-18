/**
 * lv_conf.h — LVGL 8.3.x configuration for the Workbench Reader project.
 *
 * This file is required because the project builds with
 * -D LV_CONF_INCLUDE_SIMPLE (see platformio.ini), which tells LVGL to
 * look for a top-level "lv_conf.h" instead of using its bundled
 * lv_conf_template.h. Without this file, LVGL falls back to template
 * defaults that disable most font sizes (the project uses 8/10/12/14/
 * 16/18/48) and may not match the color depth / feature set the rest
 * of the firmware assumes.
 *
 * Place this file at the project root (alongside platformio.ini) —
 * PlatformIO's default LDF search path picks up a root-level
 * lv_conf.h automatically for any library that does
 * #include "lv_conf.h" (which LVGL does internally when
 * LV_CONF_INCLUDE_SIMPLE is defined).
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

// 16-bit RGB565 — matches the ILI-class SPI TFT this project targets.
// ui_engine.cpp's canvas_save_bmp() reads lv_color_t.ch.red/green/blue,
// which is only a valid union member at this color depth.
#define LV_COLOR_DEPTH 16

// Most SPI TFTs (incl. the common ILI9488/ILI9341 family) expect the
// byte-swapped order over SPI. TFT_eSPI's pushColors() in main.cpp's
// my_disp_flush() already assumes this — leave at 0 unless colors
// appear swapped on real hardware, then flip to 1.
#define LV_COLOR_16_SWAP 0

#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*=========================
   MEMORY SETTINGS
 *=========================*/

// Use the project's own PSRAM-backed allocator path rather than LVGL's
// internal static pool, since canvas/book/AI buffers already lean on
// ps_malloc() elsewhere in this codebase.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

#define LV_MEM_BUF_MAX_NUM 16
#define LV_MEMCPY_MEMSET_STD 1

/*====================
   HAL SETTINGS
 *====================*/

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

/*=======================
 * FEATURE CONFIGURATION
 *======================*/

#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4

#define LV_LAYER_SIMPLE_BUF_SIZE (24 * 1024)
#define LV_IMG_CACHE_DEF_SIZE 1
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0

#define LV_DISP_ROT_MAX_BUF (10*1024)

/*-------------
 * GPU
 *-----------*/
#define LV_USE_GPU_ARM2D 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/*-------------
 * Logging
 *-----------*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    #define LV_LOG_TRACE_MEM        1
    #define LV_LOG_TRACE_TIMER      1
    #define LV_LOG_TRACE_INDEV      1
    #define LV_LOG_TRACE_DISP_REFR  1
    #define LV_LOG_TRACE_EVENT      1
    #define LV_LOG_TRACE_OBJ_CREATE 1
    #define LV_LOG_TRACE_LAYOUT     1
    #define LV_LOG_TRACE_ANIM       1
#endif

/*-------------
 * Asserts
 *-----------*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/*-------------
 * Others
 *-----------*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

#define LV_SPRINTF_CUSTOM 0
#define LV_SPRINTF_USE_FLOAT 0

#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0

/*=====================
 *  COMPILER SETTINGS
 *====================*/
#define LV_BIG_ENDIAN_SYSTEM 0
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TIMER_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_DMA
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_USE_LARGE_COORD 0

/*==================
 *   FONT USAGE
 *==================
 * Every size used across ui_engine.cpp / reader_ui.cpp must be enabled
 * here, or those files fail with "lv_font_montserrat_N was not
 * declared in this scope" — this is the font half of the original
 * build failure (the other half being the LVGL 9 vs 8 mismatch).
 */
#define LV_FONT_MONTSERRAT_8  1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0

#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

#define LV_FONT_CUSTOM_DECLARE

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 *  WIDGET USAGE
 *==================
 * Every widget type actually used in ui_engine.cpp / reader_ui.cpp is
 * enabled below. Anything not used by this project is left off to
 * keep flash usage down.
 */
#define LV_USE_ARC        1   // battery gauge (build_tab_sys)
#define LV_USE_BAR        1   // battery bar, progress bars, sliders' base
#define LV_USE_BTN        1   // every button across all tabs
#define LV_USE_BTNMATRIX  1   // required internally by several widgets
#define LV_USE_CANVAS     1   // Draw tab — required for LV_CANVAS_BUF_SIZE_TRUE_COLOR etc.
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1   // required by canvas/label internals
#define LV_USE_LABEL      1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 0
    #define LV_LABEL_LONG_TXT_HINT 1
#endif
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     1   // brush size, brightness, charge limit
#define LV_USE_SWITCH     1   // overcharge protection toggle
#define LV_USE_TEXTAREA   1   // notes, AI chat input, Wi-Fi password, API key
#if LV_USE_TEXTAREA != 0
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500
#endif
#define LV_USE_TABLE      0

/*==================
 * EXTRA COMPONENTS
 *==================*/
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   1   // textarea password/text entry on a touch-only device
#define LV_USE_LED        0
#define LV_USE_LIST       0   // we build lists manually with lv_obj_create rows
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX      1   // available for future use; cheap to enable
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     1   // AI modal loading spinner
#define LV_USE_TABVIEW     1   // the whole tab navigation system
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

/*-----------
 * Themes
 *----------*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 0

/*-----------
 * Layouts
 *----------*/
#define LV_USE_FLEX 1   // tool panel, theme/font button rows, settings tab
#define LV_USE_GRID 0

/*---------------------
 * 3rd party libraries
 *--------------------*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

/*-----------
 * Others
 *----------*/
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_MSG 0
#define LV_USE_IME_PINYIN 0

/*=================
 *   EXAMPLES & DEMOS
 *=================*/
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */