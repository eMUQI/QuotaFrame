// Host stand-ins for the epdiy calls made by display_ui.cpp.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
enum EpdDrawMode { MODE_DU = 1, MODE_GC16 = 2, MODE_GL16 = 5 };
enum EpdDrawError { EPD_DRAW_SUCCESS = 0, EPD_DRAW_EMPTY_LINE_QUEUE = 0x400 };
typedef struct { uint8_t *front_fb, *back_fb; } EpdiyHighlevelState;
int epd_width(void);
int epd_height(void);
void epd_set_rotation(enum EpdRotation rotation);
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *framebuffer);
void epd_poweron(void);
void epd_poweroff(void);
void epd_clear(void);
void epd_lcd_set_prefill_lines(int lines);
uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *state);
void epd_hl_set_all_white(EpdiyHighlevelState *state);
enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature);
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature);
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature);
#ifdef __cplusplus
}
#endif
