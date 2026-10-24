#pragma once
#include "esp_err.h"

enum gpio_num_t : int {};
typedef enum { GPIO_DRIVE_CAP_0, GPIO_DRIVE_CAP_1, GPIO_DRIVE_CAP_2, GPIO_DRIVE_CAP_3 } gpio_drive_cap_t;

esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t cap);
esp_err_t gpio_pullup_en(gpio_num_t pin);
