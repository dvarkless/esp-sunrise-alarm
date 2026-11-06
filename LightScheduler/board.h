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
// Разводка платы Wemos D1 mini; PIN_* одинаковы для обеих платформ и заданы
// безусловно — выбор пинов от платформы не зависит.
// D4 (GPIO2) и D8 (GPIO15) — strapping-пины, это осознанный выбор разводки:
//  - GPIO2 при старте должен быть HIGH: кнопка на GND с INPUT_PULLUP не мешает,
//    пока не нажата (нажатие в момент старта может сорвать загрузку); на D4
//    также висит встроенный светодиод платы;
//  - GPIO15 при старте должен быть LOW (есть подтяжка к GND), ШИМ на нём
//    работает после загрузки.
// GPIO16 (D0) не поддерживает ШИМ, поэтому PWM выведен на D8;
// D1/D2 = GPIO5/GPIO4 — штатный I2C.
#define PIN_PWM_LED   15    // D8: PWM-выход на MOSFET/драйвер светодиода (GPIO15)
#define PIN_I2C_SDA   4     // D2: I2C SDA (общая шина BME280 + AHT + DS3231)
#define PIN_I2C_SCL   5     // D1: I2C SCL
#define PIN_BUTTON    2     // D4: кнопка, второй вывод на GND, INPUT_PULLUP
