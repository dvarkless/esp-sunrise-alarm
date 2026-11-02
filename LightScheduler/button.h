#pragma once
#include <Arduino.h>

// ===================== Кнопка (EncButton) =====================
void button_setup();  // init пина (INPUT_PULLUP, LOW) + порог удержания 2 с
void button_tick();   // вызывать один раз за цикл loop

// События опрашиваются после button_tick().
// Клик — кнопка отпущена раньше порога удержания; после удержания клик не генерируется.
bool button_click();  // короткий клик — отложить розжиг на config.snoozeMinutes
bool button_hold();   // удержание >= 2 с — погасить, отменить снуз и пропустить сегодняшний розжиг
