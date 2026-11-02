// ============================================================================
// Smart Scheduled Light Controller (ESP32)
// Главный файл: setup() и неблокирующий loop().
// Расписание — по дням недели (config.days[7], 0=Пн ... 6=Вс) + общий выключатель.
// Кнопка: клик — отложить розжиг на config.snoozeMinutes, удержание >= 2 с — пропустить сегодня.
// Каждая подсистема вынесена в свой модуль (config, sensors, light, button, webui).
// ============================================================================

#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "light.h"
#include "button.h"
#include "webui.h"

// Имена дней недели для логов: индекс 0=Пн ... 6=Вс (ISO 8601)
static const char* const WEEKDAY_NAMES[7] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};

// Состояние планировщика (хранится здесь, в главном цикле)
static bool     alarmTriggered = false;  // будильник уже сработал в текущую минуту
static uint8_t  lastMinute     = 255;    // последняя обработанная минута (для детекта смены)

// Состояние кнопки: отложенный розжиг и разовый пропуск сегодняшнего дня
static bool     snoozePending = false;  // идёт отсчёт отложенного розжига
static uint32_t snoozeStartMs = 0;      // millis() начала отсчёта
static uint32_t skipDateKey   = 0;      // ГГГГММДД дня, розжиг в который пропущен (0 — нет)

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
        // Клик = всегда погасить и отложить розжиг на config.snoozeMinutes.
        // Мгновенный ручной розжиг остался только в веб-интерфейсе («Розжечь сейчас»).
        light_stop();
        snoozePending = false;  // отменяем предыдущий отсчёт, если был
        snoozeStartMs = millis();
        snoozePending = true;
        Serial.printf("Кнопка: отложено на %u мин\r\n",
                      (unsigned)(config.snoozeMinutes ? config.snoozeMinutes : 1));
    }
    if (button_hold()) {
        // Удержание >= 2 с = погасить, отменить снуз и пропустить только сегодняшний розжиг.
        // Пропуск хранится ключом даты: завтра ключ другой, расписание снова работает.
        light_stop();
        snoozePending = false;
        skipDateKey = sensors_date_key();
        Serial.printf("Кнопка: пропуск сегодня (%lu)\r\n", (unsigned long)skipDateKey);
    }

    // --- Расписание: раз в секунду ---
    if (sensors_tick()) {
        uint8_t curHour = sensors_hour();
        uint8_t curMin  = sensors_minute();

        // При смене минуты сбрасываем флаг — срабатывание возможно раз в минуту.
        // Смена суток всегда меняет минуту (23:59 -> 00:00), поэтому флаг сбрасывается
        // и на границе дня: следующий день не может «подавить» своё событие.
        if (curMin != lastMinute) {
            lastMinute = curMin;
            alarmTriggered = false;
        }

        // День недели: 1=Пн ... 7=Вс -> индекс массива 0=Пн ... 6=Вс
        uint8_t dayIdx = sensors_weekday() - 1;
        const DaySchedule& day = config.days[dayIdx];

        // Снуз подавляет штатное срабатывание (пользователь сам отложил розжиг),
        // skipDateKey — разовый пропуск только сегодняшнего дня: завтра ключ даты другой.
        if (config.enabled && day.enabled && !alarmTriggered && !snoozePending &&
            sensors_date_key() != skipDateKey &&
            curHour == day.hour && curMin == day.minute) {
            Serial.printf("Расписание: розжиг %s %02u:%02u\r\n", WEEKDAY_NAMES[dayIdx], curHour, curMin);
            light_startFadeIn();
            alarmTriggered = true;  // защита от повторного запуска в эту минуту
        }
    }

    // --- Отложенный розжиг: неблокирующий таймер ---
    // Ручной override: срабатывает независимо от config.enabled и выбранного дня.
    // Проверяется после расписания: если дедлайн совпал с тиком расписания, расписание
    // в этот момент ещё видит snoozePending == true и не срабатывает — розжиг один.
    if (snoozePending) {
        uint32_t snoozeMs = (uint32_t)(config.snoozeMinutes ? config.snoozeMinutes : 1) * 60000UL;
        if (millis() - snoozeStartMs >= snoozeMs) {
            Serial.println(F("Кнопка: отложенный розжиг"));
            light_startFadeIn();
            snoozePending = false;
            alarmTriggered = true;  // защита от повторного запуска в эту же минуту
        }
    }
}
