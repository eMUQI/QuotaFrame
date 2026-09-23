#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "unity_test_runner.h"

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
    while (true) vTaskDelay(portMAX_DELAY);
}
