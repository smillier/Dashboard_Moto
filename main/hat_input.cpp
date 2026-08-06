#include "hat_input.h"
#include "config.h"
#include "gopro_control.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "hat_input";

static QueueHandle_t s_evt_queue;

static void IRAM_ATTR hat_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_evt_queue, &gpio_num, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

/* Same debounce approach as buttons.c: block until release, treat an
 * immediate bounce-back-high as noise rather than a real press. */
static bool debounce_and_wait_release(gpio_num_t pin)
{
    vTaskDelay(pdMS_TO_TICKS(40));
    if (gpio_get_level(pin) != 0) {
        return false;
    }
    while (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

static void hat_input_task(void *arg)
{
    uint32_t io_num;

    for (;;) {
        if (xQueueReceive(s_evt_queue, &io_num, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!debounce_and_wait_release((gpio_num_t)io_num)) {
            continue;
        }

        if (io_num == (uint32_t)kHatKey1Pin) {
            ESP_LOGW(TAG, "HAT KEY1 pressed - reset");
            gopro_control_reset_device();
        } else if (io_num == (uint32_t)kJoyUpPin) {
            ESP_LOGD(TAG, "Joystick UP (unbound)");
        } else if (io_num == (uint32_t)kJoyDownPin) {
            ESP_LOGD(TAG, "Joystick DOWN (unbound)");
        } else if (io_num == (uint32_t)kJoyLeftPin) {
            ESP_LOGD(TAG, "Joystick LEFT (unbound)");
        } else if (io_num == (uint32_t)kJoyRightPin) {
            ESP_LOGD(TAG, "Joystick RIGHT (unbound)");
        } else if (io_num == (uint32_t)kJoyPressPin) {
            ESP_LOGD(TAG, "Joystick PRESS (unbound)");
        } else if (io_num == (uint32_t)kHatKey2Pin) {
            ESP_LOGD(TAG, "HAT KEY2 (unbound)");
        } else if (io_num == (uint32_t)kHatKey3Pin) {
            ESP_LOGD(TAG, "HAT KEY3 (unbound)");
        }
    }
}

static void configure_input(gpio_num_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, hat_isr_handler, (void *)(uintptr_t)pin));
}

void hat_input_init(void)
{
    s_evt_queue = xQueueCreate(8, sizeof(uint32_t));

    /* buttons.c may already have installed the shared GPIO ISR service;
     * ESP_ERR_INVALID_STATE just means "already installed", not an error. */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    configure_input((gpio_num_t)kJoyUpPin);
    configure_input((gpio_num_t)kJoyDownPin);
    configure_input((gpio_num_t)kJoyLeftPin);
    configure_input((gpio_num_t)kJoyRightPin);
    configure_input((gpio_num_t)kJoyPressPin);
    configure_input((gpio_num_t)kHatKey1Pin);
    configure_input((gpio_num_t)kHatKey2Pin);
    configure_input((gpio_num_t)kHatKey3Pin);

    xTaskCreate(hat_input_task, "hat_input", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "HAT input ready: KEY1(GPIO%d)=reset, joystick+KEY2/KEY3 wired but unbound",
             kHatKey1Pin);
}
