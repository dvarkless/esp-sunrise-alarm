#pragma once
#include <Arduino.h>

// ===================== Пины =====================
#define PIN_PWM_LED   5     // PWM-выход на MOSFET/драйвер светодиода (общий GND с ESP32)
#define PIN_I2C_SDA   21    // I2C SDA (общая шина BME280 + DS3231)
#define PIN_I2C_SCL   22    // I2C SCL
#define PIN_BUTTON    27    // тактовая кнопка: второй вывод на GND, внутренняя подтяжка INPUT_PULLUP

// ===================== PWM (LEDC) =====================
#define PWM_FREQ      25000  // частота ШИМ, Гц
#define PWM_RES       8     // разрешение 8 бит (0..255)
#define PWM_CHANNEL   0     // канал LEDC
#define FADE_STEP_MS  500    // период обновления скважности при розжиге, мс

// ===================== I2C =====================
#define BME280_ADDR   0x77  // адрес BME280 (0x76 или 0x77)
#define RTC_ADDR      0x68  // адрес DS3231
#define TIME_ZONE     3     // часовой пояс (UTC+3, Москва)

// ===================== WiFi =====================
// Учётные данные сети. Если SSID пустой — устройство поднимет собственную точку доступа.
#define WIFI_SSID     ""
#define WIFI_PASS     ""
#define AP_NAME       "LightScheduler"  // имя точки доступа при пустом SSID

// ===================== Config =====================
// Единый источник правды для всех настроек.
// Saver хранит эту структуру бинарно в файле /config.cfg на LittleFS (атомарная запись).
// Меняйте значения по умолчанию здесь — они используются при первом запуске/сбросе.
// Версия формата задаётся в config.cpp ('B'); при несовместимом изменении структуры — поднимать.

// Расписание одного дня недели
struct DaySchedule {
    uint8_t hour    = 7;     // час включения света (0-23)
    uint8_t minute  = 30;    // минута включения света (0-59)
    bool    enabled = true;  // день участвует в расписании
};

struct Config {
    bool        enabled       = true;   // общий выключатель расписания (мастер-переключатель)
    uint8_t     snoozeMinutes = 10;     // «отложить» после клика кнопкой, мин (1-60)
    DaySchedule days[7];                // расписание по дням: [0]=Пн ... [6]=Вс (ISO 8601)
    uint32_t    fadeDuration  = 15;  // длительность плавного розжига, мин (0-60)
    uint32_t    turnOffDelay  = 60;  // длительность свечения, мин (0-60)
};

// Глобальный экземпляр конфига (определён в config.cpp)
extern Config config;

// ===================== Модуль config =====================
void config_setup();   // LittleFS.begin() + saver.begin()
void config_tick();    // saver.tick() — автосохранение изменений
void config_save();    // принудительно записать конфиг в LittleFS
