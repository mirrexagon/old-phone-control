#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

const uint32_t GPION_OFF_HOOK = 4; // High for off-hook, low for on-hook. SHK pin on KS0835.
const uint32_t GPION_RING_MODE = 5; // RM pin on KS0835.
const uint32_t GPION_RING_WAVE = 6; // FR pin on KS0835.
const uint32_t GPION_AUDIO_OUT = 17; // DAC_1 - VR pin on KS0835.
const uint32_t GPION_AUDIO_IN = 1; // ADC1_CH0 - VX pin on KS0835.

const int ESP_INTR_FLAG_DEFAULT = 0;

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR off_hook_edge_isr(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
        }
    }
}

static void setup_ring(void)
{
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1 << GPION_RING_MODE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .freq_hz          = 25,  // Hz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = GPION_RING_WAVE,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void start_ring(void)
{
    ESP_ERROR_CHECK(gpio_set_level(GPION_RING_MODE, 1));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 4095)); // 50% at 13-bit resolution.
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static void stop_ring(void)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    ESP_ERROR_CHECK(gpio_set_level(GPION_RING_MODE, 0));
}

static void setup_shk(void)
{
    gpio_config_t io_conf = {};

    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1 << GPION_OFF_HOOK;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPION_OFF_HOOK, off_hook_edge_isr, (void*)GPION_OFF_HOOK);
}

void app_main(void)
{
    setup_shk();
    setup_ring();

    printf("Minimum free heap size: %"PRIu32" bytes\n", esp_get_minimum_free_heap_size());

    while(1) {
        printf("Ring stop\n");
        stop_ring();
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("Ring start\n");
        start_ring();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
