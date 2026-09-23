#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "epaper_port.h"

static spi_device_handle_t spi;
static uint8_t *gray_plane;
static const char *TAG = "EPD_DRIVER";

static void epaper_gpio_Init(void)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)0x01<<EPD_RST_PIN) | ((uint64_t)0x01<<EPD_DC_PIN) | ((uint64_t)0x01<<EPD_CS_PIN);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_INPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)0x01<<EPD_BUSY_PIN);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

  epaper_rst_1;
}

void epaper_port_init(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg =
    {
        .miso_io_num = -1,
        .mosi_io_num = EPD_MOSI_PIN,
        .sclk_io_num = EPD_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 65536,
    };
    spi_device_interface_config_t devcfg =
    {
        .spics_io_num = -1,
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .queue_size = 1,
    };

    gray_plane = heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(gray_plane ? ESP_OK : ESP_ERR_NO_MEM);

    ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    epaper_gpio_Init();
}

static void spi_send_byte(uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    t.length = 8;
    t.tx_buffer = &cmd;
    t.rx_buffer = NULL;
    t.rxlength = 0;

    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

static void EPD_Reset(void)
{
    epaper_rst_1;
    vTaskDelay(pdMS_TO_TICKS(50));
    epaper_rst_0;
    vTaskDelay(pdMS_TO_TICKS(2));
    epaper_rst_1;
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void EPD_SendCommand(UBYTE Reg)
{
    epaper_dc_0;
    spi_send_byte(Reg);
}

static void EPD_SendData(UBYTE Data)
{
    epaper_dc_1;
    spi_send_byte(Data);
}

static void EPD_SendDataBuffer(const UBYTE* buffer, UDOUBLE length)
{
    epaper_dc_1;

    esp_err_t ret;
    const size_t chunk_size = 4096;

    for (size_t i = 0; i < length; i += chunk_size) {
        size_t current_chunk = (i + chunk_size > length) ? (length - i) : chunk_size;

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));

        t.length = current_chunk * 8;
        t.tx_buffer = buffer + i;

        ret = spi_device_polling_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmission failed: %s", esp_err_to_name(ret));
            ESP_ERROR_CHECK(ret);
        }
    }

    ESP_LOGD(TAG, "All %lu bytes transmitted successfully", (unsigned long)length);
}

static void EPD_ReadBusy(void)
{

    vTaskDelay(pdMS_TO_TICKS(100));
    const int64_t deadline = esp_timer_get_time() + 15000000;
    while(1)
    {
        if(!ReadBusy){break;}
        ESP_ERROR_CHECK(esp_timer_get_time() < deadline ? ESP_OK : ESP_ERR_TIMEOUT);

        vTaskDelay(pdMS_TO_TICKS(20));
    }

}



static void EPD_TurnOnDisplay(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xF7);
	EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

static void EPD_TurnOnDisplay_4GRAY(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xD7);
	EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

static void EPD_TurnOnDisplay_Part(void)
{
    EPD_SendCommand(0x22);
    EPD_SendData(0xFF);
    EPD_SendCommand(0x20);
    EPD_ReadBusy();
}

void EPD_Init(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    EPD_Reset();

    EPD_ReadBusy();
    EPD_SendCommand(0x12);
    EPD_ReadBusy();

    EPD_SendCommand(0x18);
    EPD_SendData(0x80);

    EPD_SendCommand(0x0C);
	EPD_SendData(0xAE);
	EPD_SendData(0xC7);
	EPD_SendData(0xC3);
	EPD_SendData(0xC0);
	EPD_SendData(0x80);

    EPD_SendCommand(0x01);
    EPD_SendData((EPD_HEIGHT-1)%256);
    EPD_SendData((EPD_HEIGHT-1)/256);
    EPD_SendData(0x02);

    EPD_SendCommand(0x3C);
    EPD_SendData(0x01);

    EPD_SendCommand(0x11);
	EPD_SendData(0x01);

	EPD_SendCommand(0x44);
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendData((EPD_WIDTH-1)%256);
	EPD_SendData((EPD_WIDTH-1)/256);

	EPD_SendCommand(0x45);
    EPD_SendData((EPD_HEIGHT-1)%256);
	EPD_SendData((EPD_HEIGHT-1)/256);
	EPD_SendData(0x00);
	EPD_SendData(0x00);

    EPD_SendCommand(0x4E);
	EPD_SendData(0x00);
	EPD_SendData(0x00);
    // Decrementing gate addresses start at the top framebuffer row.
    EPD_SendCommand(0x4F);
    EPD_SendData((EPD_HEIGHT - 1) & 0xFF);
    EPD_SendData(((EPD_HEIGHT - 1) >> 8) & 0xFF);
    EPD_ReadBusy();

}

void EPD_Init_4GRAY(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
	EPD_Reset();

	EPD_ReadBusy();
	EPD_SendCommand(0x12);
	EPD_ReadBusy();

	EPD_SendCommand(0x0C);
	EPD_SendData(0xAE);
	EPD_SendData(0xC7);
	EPD_SendData(0xC3);
	EPD_SendData(0xC0);
	EPD_SendData(0x80);

	EPD_SendCommand(0x01);
	EPD_SendData((EPD_HEIGHT-1)%256);
	EPD_SendData((EPD_HEIGHT-1)/256);
	EPD_SendData(0x02);

	EPD_SendCommand(0x11);
	EPD_SendData(0x01);

	EPD_SendCommand(0x44);
	EPD_SendData(0x00);
	EPD_SendData(0x00);
	EPD_SendData((EPD_WIDTH-1)%256);
	EPD_SendData((EPD_WIDTH-1)/256);

	EPD_SendCommand(0x45);
    EPD_SendData((EPD_HEIGHT-1)%256);
	EPD_SendData((EPD_HEIGHT-1)/256);
	EPD_SendData(0x00);
	EPD_SendData(0x00);

	EPD_SendCommand(0x4E);
	EPD_SendData(0x00);
	EPD_SendData(0x00);
    // Decrementing gate addresses start at the top framebuffer row.
    EPD_SendCommand(0x4F);
    EPD_SendData((EPD_HEIGHT - 1) & 0xFF);
    EPD_SendData(((EPD_HEIGHT - 1) >> 8) & 0xFF);
    EPD_ReadBusy();

	EPD_SendCommand(0x3C);
	EPD_SendData(0x01);

	EPD_SendCommand(0x18);
	EPD_SendData(0x80);

	EPD_SendCommand(0x1A);
	EPD_SendData(0x5A);

}

void EPD_Display_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;
    UDOUBLE buffer_size = Width * Height;

    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(Image, buffer_size);


    EPD_SendCommand(0x26);
    EPD_SendDataBuffer(Image, buffer_size);

    EPD_TurnOnDisplay();
}

static void EPD_SetRamPointer(UWORD x, UWORD y)
{
    EPD_SendCommand(0x4E);
    EPD_SendData(x & 0xFF);
    EPD_SendData((x >> 8) & 0xFF);
    EPD_SendCommand(0x4F);
    EPD_SendData(y & 0xFF);
    EPD_SendData((y >> 8) & 0xFF);
}

void EPD_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    assert(Image && Xstart < Xend && Ystart < Yend);
    assert(Xend <= EPD_WIDTH && Yend <= EPD_HEIGHT);
    assert(Xstart % 8 == 0 && Xend % 8 == 0);

    const UDOUBLE image_size = ((Xend - Xstart) / 8) * (Yend - Ystart);
    const UWORD x_last = Xend - 1;
    // Panel gate wiring maps top-to-bottom framebuffer rows to descending RAM addresses.
    const UWORD y_first = EPD_HEIGHT - 1 - Ystart;
    const UWORD y_last = EPD_HEIGHT - Yend;

    EPD_Reset();

    EPD_SendCommand(0x18);
    EPD_SendData(0x80);

    EPD_SendCommand(0x3C);
    EPD_SendData(0x80);

    EPD_SendCommand(0x11);
    EPD_SendData(0x01);

    EPD_SendCommand(0x44);
    EPD_SendData(Xstart & 0xFF);
    EPD_SendData((Xstart >> 8) & 0xFF);
    EPD_SendData(x_last & 0xFF);
    EPD_SendData((x_last >> 8) & 0xFF);

    EPD_SendCommand(0x45);
    EPD_SendData(y_first & 0xFF);
    EPD_SendData((y_first >> 8) & 0xFF);
    EPD_SendData(y_last & 0xFF);
    EPD_SendData((y_last >> 8) & 0xFF);

    EPD_SetRamPointer(Xstart, y_first);

    ESP_LOGI(TAG, "PARTIAL x=%u..%u y=%u..%u ram_y=%u..%u bytes=%lu",
             Xstart, x_last, Ystart, Yend - 1, y_first, y_last, (unsigned long)image_size);
    EPD_SendCommand(0x24);
    EPD_SendDataBuffer(Image, image_size);

    EPD_TurnOnDisplay_Part();

    // Both RAM planes must match the displayed region before the next differential refresh.
    const UBYTE planes[] = {0x26, 0x24};
    for (unsigned i = 0; i < sizeof(planes); ++i) {
        EPD_SetRamPointer(Xstart, y_first);
        EPD_SendCommand(planes[i]);
        EPD_SendDataBuffer(Image, image_size);
    }
}

void EPD_PrepareMonoRam(const UBYTE *image)
{
    // RAM conversion preserves optical content for isolated black-white partial windows.
    EPD_Init();
    const UBYTE planes[] = {0x26, 0x24};
    for (unsigned i = 0; i < sizeof(planes); ++i) {
        EPD_SetRamPointer(0, EPD_HEIGHT - 1);
        EPD_SendCommand(planes[i]);
        EPD_SendDataBuffer(image, EPD_SIZE_MONO);
    }
}

void EPD_Display_4Gray(const UBYTE *image)
{
    uint8_t *plane = gray_plane;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < EPD_SIZE_MONO; ++i) {
            uint8_t value = 0;
            for (int bit = 0; bit < 8; ++bit) {
                const int pixel = i * 8 + bit;
                const uint8_t gray = (image[pixel / 4] >> (6 - (pixel % 4) * 2)) & 3;
                value = (value << 1) | (pass == 0 ? (gray == 0 || gray == 2) : (gray == 0 || gray == 1));
            }
            plane[i] = value;
        }
        EPD_SendCommand(pass == 0 ? 0x24 : 0x26);
        EPD_SendDataBuffer(plane, EPD_SIZE_MONO);
    }
    EPD_TurnOnDisplay_4GRAY();
}

void EPD_Sleep(void)
{
    EPD_SendCommand(0x10);
    EPD_SendData(0x01);
    vTaskDelay(pdMS_TO_TICKS(10));
    epaper_rst_0;
    epaper_cs_0;
    epaper_dc_0;
    vTaskDelay(pdMS_TO_TICKS(10));
}
