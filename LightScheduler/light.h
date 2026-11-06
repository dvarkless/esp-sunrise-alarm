#pragma once
#include <Arduino.h>

// ===================== Свет: плавный розжиг через ШИМ (LEDC/analogWrite) =====================
// Кривая розжига перцептуальная: d = p^1.7369656 (50 % прогресса → 30 % скважности).
// Формула, таблица и обоснование — docs/light-curve.md.
void light_setup();        // pwm_setup() (LEDC на ESP32, analogWrite на ESP8266), свет выключен
void light_startFadeIn();  // запустить плавный розжиг с нуля до максимума (config.maxBrightness)
void light_stop();         // мгновенно погасить и остановить розжиг
void light_update();       // неблокирующий тик розжига (вызывать в loop)
bool light_active();       // true, пока идёт розжиг (после завершения — false)
bool light_isOn();         // true, если свет включён: с light_startFadeIn до light_stop (в т.ч. после розжига)
