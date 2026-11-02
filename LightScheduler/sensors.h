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
