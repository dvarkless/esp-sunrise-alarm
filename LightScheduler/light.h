#pragma once
#include <Arduino.h>

// ===================== Свет: плавный розжиг через LEDC (PWM) =====================
void light_setup();        // ledcSetup + ledcAttachPin, свет выключен
void light_startFadeIn();  // запустить плавный розжиг с нуля до максимума
void light_stop();         // мгновенно погасить и остановить розжиг
void light_update();       // неблокирующий тик розжига (вызывать в loop)
bool light_active();       // true, пока идёт розжиг
