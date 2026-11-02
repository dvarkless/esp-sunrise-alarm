#pragma once
#include <Arduino.h>

// ===================== Веб-интерфейс (Settings / SettingsGyver) =====================
void webui_setup();  // подключение к WiFi + запуск веб-сервера настроек
void webui_tick();   // вызывать в loop
