#include "sensors.h"
#include "config.h"

#include <Wire.h>
#include <GyverBME280.h>
#include <GyverDS3231.h>

GyverBME280 bme;
GyverDS3231 rtc;

// Кэш последних измерений (обновляется раз в секунду в sensors_tick)
static float temp = 0.0f;
static float hum  = 0.0f;
static float pres = 0.0f;
static bool  ok   = false;

void sensors_setup() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    setStampZone(TIME_ZONE);  // часовой пояс для DS3231

    rtc.begin();
    if (!rtc.isOK()) {
        Serial.println(F("Sensors: DS3231 не найден на шине I2C"));
    } else if (rtc.isReset()) {
        // Время RTC не установлено (сброс питания) — ставим время компиляции прошивки
        rtc.setBuildTime();
        Serial.println(F("Sensors: RTC был сброшен, установлено время компиляции"));
    }

    ok = bme.begin(BME280_ADDR);
    Serial.println(ok ? F("Sensors: BME280 OK") : F("Sensors: BME280 не найден"));
}

// Тикер: вызывать в loop. Возвращает true раз в секунду, если RTC синхронизирован.
bool sensors_tick() {
    bool second = rtc.tick();

    if (second && ok) {
        temp = bme.readTemperature();
        hum  = bme.readHumidity();
        pres = bme.readPressure() / 100.0f;  // Па -> гПа
    }

    return second;
}

bool  sensors_ok()          { return ok; }
float sensors_temperature() { return temp; }
float sensors_humidity()    { return hum; }
float sensors_pressure()    { return pres; }
uint8_t sensors_hour()      { return rtc.hour(); }
uint8_t sensors_minute()    { return rtc.minute(); }

// День недели и дату берём из кэша StampKeeper (Datime из unix), без лишних обращений
// к DS3231 по I2C; тот же источник времени, что и у sensors_hour()/sensors_minute().
uint8_t sensors_weekday() {
    // Без синхронизированного RTC день недели не определён — отдаём Пн (1).
    // Планировщик при этом не работает: sensors_tick() возвращает true только при synced RTC.
    return rtc.synced() ? rtc.weekDay() : 1;  // Datime::weekDay(): 1=Пн ... 7=Вс
}

uint32_t sensors_date_key() {
    if (!rtc.synced()) return 0;  // нет валидного календарного дня
    return (uint32_t)rtc.year() * 10000UL + (uint32_t)rtc.month() * 100UL + rtc.day();
}
