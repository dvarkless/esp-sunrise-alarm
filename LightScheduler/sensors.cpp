#include "sensors.h"
#include "config.h"
#include "log.h"

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <GyverBME280.h>
#include <GyverDS3231.h>

GyverBME280 bme;
GyverDS3231 rtc;

// AHT10/AHT20 — датчик влажности на общей шине I2C (температура и давление остаются на BME280).
Adafruit_AHTX0 aht;

// Кэш последних измерений (обновляется раз в секунду в sensors_tick)
static float temp = 0.0f;
static float hum  = 0.0f;
static float pres = 0.0f;
static bool  ok   = false;

// --- AHT10/AHT20: источник влажности ---
// Адрес AHT10/AHT20 по умолчанию — 0x38 (0x39 — альтернативный адрес, если задан перемычкой).
static const uint8_t AHT_ADDR = 0x38;

// Протокол AHT10/AHT20 (даташит): команда запуска измерения 0xAC 0x33 0x00; статус читается
// напрямую одним байтом (бит 0x80 — «занято»); ответ — 6 байт, влажность — 20-битное слово:
// RH% = raw20 * 100 / 2^20, raw20 = data[1]<<12 | data[2]<<4 | data[3]>>4.
static const uint8_t  AHT_CMD_TRIGGER = 0xAC;
static const uint8_t  AHT_STATUS_BUSY = 0x80;
static const uint32_t AHT_PERIOD_MS   = 2000;   // период измерений влажности, мс
static const uint32_t AHT_POLL_MS     = 100;    // минимальный интервал опроса статуса, мс
static const uint32_t AHT_TIMEOUT_MS  = 500;    // сброс зависшего измерения, мс
static const uint32_t AHT_STALE_MS    = 10000;  // кэш старше — влажность с BME280 (запасной), мс

// Состояние неблокирующего автомата AHT (измерение идёт между проходами loop()).
static bool     ahtOk      = false;  // AHT инициализирован в sensors_setup()
static bool     ahtValid   = false;  // есть хотя бы одно успешное измерение
static bool     ahtBusy    = false;  // триггер выдан, ждём готовности
static float    ahtHum     = 0.0f;   // последняя влажность с AHT, %
static bool     ahtStarted = false;  // первое измерение запускается сразу после старта
static uint32_t ahtRunMs   = 0;      // millis() последнего запуска измерения
static uint32_t ahtStartMs = 0;      // millis() запуска текущего измерения
static uint32_t ahtPollMs  = 0;      // millis() последнего опроса статуса
static uint32_t ahtGoodMs  = 0;      // millis() последнего успешного чтения

// Фильтр IIR BME280 (регистр 0xF5): 0=выкл, 1=×2, 2=×4, 3=×8, 4=×16.
// В GyverBME280 по умолчанию уже x16 — вызов задаёт коэффициент явно и делает настройку настраиваемой.
// Важно: setFilter() обязан идти ДО begin() — коэффициент записывается в 0xF5 внутри begin().
static const uint8_t BME_FILTER_MODE = FILTER_COEF_16;

void sensors_setup() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    setStampZone(TIME_ZONE);  // часовой пояс для DS3231

    rtc.begin();
    if (!rtc.isOK()) {
        log_println(F("Sensors: DS3231 не найден на шине I2C"));
    } else if (rtc.isReset()) {
        // Время RTC не установлено (сброс питания) — ставим время компиляции прошивки
        //rtc.setBuildTime();
        log_println(F("Sensors: RTC был сброшен!"));
    }

    bme.setFilter(BME_FILTER_MODE);  // IIR x16, см. BME_FILTER_MODE
    ok = bme.begin(BME280_ADDR);
    log_println(ok ? F("Sensors: BME280 OK") : F("Sensors: BME280 не найден"));

    // AHT10/AHT20 — основной источник влажности. Блокирующие задержки внутри begin() здесь допустимы:
    // это setup(), а не loop(). Если AHT не отвечает — влажность продолжит приходить с BME280.
    ahtOk = aht.begin(&Wire, 0, AHT_ADDR);
    log_println(ahtOk ? F("Sensors: AHT10/20 OK — влажность с AHT")
                      : F("Sensors: AHT10/20 не найден — влажность с BME280 (запасной вариант)"));
}

// Неблокирующий шаг AHT10/AHT20: триггер -> опрос готовности -> чтение 6 байт -> кэш.
// Вызывается на каждом проходе loop(); без delay() и без ожиданий в while.
static void aht_tick() {
    if (!ahtOk) return;

    uint32_t now = millis();

    // 1) Запуск нового измерения — не чаще AHT_PERIOD_MS (первый запуск сразу после старта)
    if (!ahtBusy) {
        if (ahtStarted && (uint32_t)(now - ahtRunMs) < AHT_PERIOD_MS) return;
        ahtStarted = true;
        ahtRunMs   = now;
        Wire.beginTransmission(AHT_ADDR);
        Wire.write(AHT_CMD_TRIGGER);
        Wire.write(0x33);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0) return;  // NACK: датчик не ответил — попробуем в следующий период
        ahtBusy    = true;
        ahtStartMs = now;
        ahtPollMs  = now;
        return;
    }

    // 2) Опрос статуса — не чаще AHT_POLL_MS, одно короткое чтение. Бит 0x80 — «измеряет».
    if ((uint32_t)(now - ahtPollMs) < AHT_POLL_MS) return;
    ahtPollMs = now;

    if (Wire.requestFrom(AHT_ADDR, (size_t)1) != 1) {
        ahtBusy = false;  // нет ответа — прерываем измерение, новый запуск будет позже
        return;
    }
    if (Wire.read() & AHT_STATUS_BUSY) {
        // Ещё измеряет. Защита от зависания: по таймауту отпускаем автомат.
        if ((uint32_t)(now - ahtStartMs) > AHT_TIMEOUT_MS) ahtBusy = false;
        return;
    }

    // 3) Готово: 6 байт (статус + 20 бит влажности + 20 бит температуры; температура не нужна)
    if (Wire.requestFrom(AHT_ADDR, (size_t)6) != 6) {
        ahtBusy = false;
        return;
    }
    uint8_t data[6];
    for (uint8_t i = 0; i < 6; i++) data[i] = (uint8_t)Wire.read();

    uint32_t raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    ahtHum    = (float)raw * 100.0f / 1048576.0f;  // 2^20 = 0x100000
    ahtValid  = true;
    ahtGoodMs = now;
    ahtBusy   = false;
}

// Тикер: вызывать в loop. Возвращает true раз в секунду, если RTC синхронизирован.
bool sensors_tick() {
    aht_tick();  // неблокирующий автомат AHT10/AHT20 (источник влажности)

    bool second = rtc.tick();

    if (second && ok) {
        temp = bme.readTemperature();
        pres = bme.readPressure() / 100.0f;  // Па -> гПа
        // Влажность: основной источник — AHT, запасной — BME280 (пока AHT нет или данные устарели)
        if (ahtOk && ahtValid && (uint32_t)(millis() - ahtGoodMs) < AHT_STALE_MS) {
            hum = ahtHum;
        } else {
            hum = bme.readHumidity();
        }
    }

    return second;
}

bool  sensors_ok()          { return ok; }
float sensors_temperature() { return temp; }
float sensors_humidity()    { return hum; }
float sensors_pressure()    { return pres; }
uint8_t sensors_hour()      { return rtc.hour(); }
uint8_t sensors_minute()    { return rtc.minute(); }
String sensors_date_str() { return rtc.dateToString(); }
String sensors_time_str() { return rtc.timeToString(); }

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
