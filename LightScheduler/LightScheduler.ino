// ============================================================================
// Smart Scheduled Light Controller (ESP32)
// Главный файл: setup() и неблокирующий loop().
// Каждая подсистема вынесена в свой модуль (config, sensors, light, button, webui).
// ============================================================================

#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "light.h"
#include "button.h"
#include "webui.h"

// Состояние планировщика (хранится здесь, в главном цикле)
static bool     alarmTriggered = false;  // будильник уже сработал в текущую минуту
static uint8_t  lastMinute     = 255;    // последняя обработанная минута (для детекта смены)

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println(F("Smart Scheduled Light Controller"));

    config_setup();   // LittleFS + загрузка/сохранение конфига
    sensors_setup();  // I2C, BME280, DS3231
    light_setup();    // LEDC PWM
    button_setup();   // EncButton
    webui_setup();    // WiFi + веб-интерфейс

    Serial.println(F("Готово"));
}

void loop() {
    // Тикеры всех модулей — всё неблокирующее
    config_tick();
    light_update();
    button_tick();
    webui_tick();

    // --- Кнопка ---
    if (button_click()) {
        Serial.println(F("Кнопка: запуск розжига"));
        light_startFadeIn();
    }
    if (button_hold()) {
        Serial.println(F("Кнопка: отмена/сброс"));
        light_stop();
        alarmTriggered = false;
    }

    // --- Расписание: раз в секунду ---
    if (sensors_tick()) {
        uint8_t curHour = sensors_hour();
        uint8_t curMin  = sensors_minute();

        // При смене минуты сбрасываем флаг — будильник срабатывает каждый день
        if (curMin != lastMinute) {
            lastMinute = curMin;
            alarmTriggered = false;
        }

        if (config.enabled && !alarmTriggered &&
            curHour == config.scheduleHour &&
            curMin  == config.scheduleMin) {
            Serial.println(F("Расписание: сработал будильник, запуск розжига"));
            light_startFadeIn();
            alarmTriggered = true;  // защита от повторного запуска в эту минуту
        }
    }
}
