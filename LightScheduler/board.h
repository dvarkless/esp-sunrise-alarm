#pragma once
#include <Arduino.h>

// ===================== Платформа =====================
// Целевая плата определяется макросами тулчейна: Arduino добавляет -DARDUINO_ARCH_{arch}
// в каждую команду сборки, а ядра ESP8266/ESP32 дополнительно -DESP8266 / -DESP32=ESP32.
// ESP8266 проверяем первым — house style диспетчеризации платформ (см. GyverIO и
// docs/code-style.md §5).
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#define LS_BOARD_ESP8266
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#define LS_BOARD_ESP32
#else
#error "Поддерживаются только ESP32 и ESP8266"
#endif

// ===================== Пины =====================
// ESP8266 (NodeMCU/Wemos D1 mini): обычные GPIO, не strapping-пины 0/2/12/15/16,
// не UART 1/3 и не ноги flash 6-11; D1/D2 — штатные I2C, D5/D7 свободны.
// ESP32: прежняя разводка платы проекта (значения не менялись).
#ifdef LS_BOARD_ESP8266
#define PIN_PWM_LED   14    // D5: PWM-выход на MOSFET/драйвер светодиода
#define PIN_I2C_SDA   4     // D2: I2C SDA (общая шина BME280 + AHT + DS3231)
#define PIN_I2C_SCL   5     // D1: I2C SCL
#define PIN_BUTTON    13    // D7: кнопка, второй вывод на GND, INPUT_PULLUP
#else
#define PIN_PWM_LED   5     // PWM-выход на MOSFET/драйвер светодиода (общий GND с ESP32)
#define PIN_I2C_SDA   21    // I2C SDA (общая шина BME280 + AHT + DS3231)
#define PIN_I2C_SCL   22    // I2C SCL
#define PIN_BUTTON    27    // кнопка, второй вывод на GND, INPUT_PULLUP
#endif
