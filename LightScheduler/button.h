#pragma once
#include <Arduino.h>

// ===================== Кнопка (EncButton) =====================
void button_setup();  // init пина (INPUT_PULLUP, активный уровень LOW)
void button_tick();   // вызывать один раз за цикл loop

// События опрашиваются после button_tick()
bool button_click();  // короткий клик — ручной запуск розжига
bool button_hold();   // удержание (> 500 мс) — отмена/сброс
