#ifndef EPAPER_DRIVER_H
#define EPAPER_DRIVER_H
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t UDOUBLE;






/**
 * GPIO config
**/
#define EPD_SCLK_PIN    11
#define EPD_MOSI_PIN    12

#define EPD_CS_PIN      10

#define EPD_DC_PIN      9

#define EPD_RST_PIN     46
#define EPD_BUSY_PIN    3



#define epaper_rst_1    gpio_set_level(EPD_RST_PIN,1)
#define epaper_rst_0    gpio_set_level(EPD_RST_PIN,0)
#define epaper_cs_1     gpio_set_level(EPD_CS_PIN,1)
#define epaper_cs_0     gpio_set_level(EPD_CS_PIN,0)
#define epaper_dc_1     gpio_set_level(EPD_DC_PIN,1)
#define epaper_dc_0     gpio_set_level(EPD_DC_PIN,0)

#define ReadBusy        gpio_get_level(EPD_BUSY_PIN)




// Display resolution
#define EPD_WIDTH               800
#define EPD_HEIGHT              480
#define EPD_SIZE_MONO           48000

#ifdef __cplusplus
extern "C" {
#endif

void epaper_port_init(void);

void EPD_Init(void);
void EPD_Init_4GRAY(void);
void EPD_Display_Base(const UBYTE *Image);
/**
 * Refresh a tightly packed monochrome rectangle in native framebuffer coordinates.
 * End coordinates are exclusive; both X boundaries must be multiples of 8.
 * Image must be non-NULL and the nonempty rectangle must be within the panel.
 * Requires matching monochrome RAM and optical content throughout the window, without sleep.
 * EPD_Display_Base establishes this globally; EPD_PrepareMonoRam supports isolated black-white windows.
 * Returns after refresh completion and synchronization of both RAM planes.
 */
void EPD_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend);
// Initializes monochrome mode and both RAM planes without refreshing optical content.
// Partial updates must remain inside optically black-white regions matching Image.
void EPD_PrepareMonoRam(const UBYTE *Image);
void EPD_Display_4Gray(const UBYTE *Image);
void EPD_Sleep(void);

#ifdef __cplusplus
}
#endif



#endif // !EPAPER_DRIVER_H
