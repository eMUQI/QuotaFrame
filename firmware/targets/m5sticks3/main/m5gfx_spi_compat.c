#include <stdint.h>

#include "driver/spi_common.h"

esp_err_t __real_spi_bus_initialize(spi_host_device_t host_id,
                                    const spi_bus_config_t *bus_config,
                                    spi_dma_chan_t dma_chan);

esp_err_t __wrap_spi_bus_initialize(spi_host_device_t host_id,
                                    const spi_bus_config_t *bus_config,
                                    spi_dma_chan_t dma_chan)
{
    // M5GFX fills the configuration with 0xFF but leaves IDF 6.1's DMA burst field unset.
    // Zero selects the SPI driver's default burst size.
    if (bus_config != NULL && bus_config->dma_burst_size == UINT32_MAX) {
        spi_bus_config_t config = *bus_config;
        config.dma_burst_size = 0;
        return __real_spi_bus_initialize(host_id, &config, dma_chan);
    }
    return __real_spi_bus_initialize(host_id, bus_config, dma_chan);
}
