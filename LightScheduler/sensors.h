#pragma once
#include <Arduino.h>

// ===================== Датчики: BME280 + DS3231 =====================
void  sensors_setup();        // Wire + BME280 + DS3231 (при первом запуске ставит время компиляции)
bool  sensors_tick();         // вызывать в loop: вернёт true раз в секунду (и обновит кэш датчиков)

bool  sensors_ok();           // BME280 инициализирован и доступен
float sensors_temperature();  // температура, °C
float sensors_humidity();     // влажность, %
float sensors_pressure();     // давление, гПа

uint8_t sensors_hour();       // текущий час по RTC (0-23)
uint8_t sensors_minute();     // текущая минута по RTC (0-59)

// День недели по RTC: 1=Пн ... 7=Вс (ISO 8601). При несинхронизированном RTC — 1 (Пн),
// но расписание в этом режиме не тикает: sensors_tick() не вернёт true.
uint8_t sensors_weekday();
// Ключ календарного дня ГГГГММДД (например, 20261102) для детекта смены суток и «пропуска
// сегодня». 0 — RTC не синхронизирован, валидного дня нет.
uint32_t sensors_date_key();
