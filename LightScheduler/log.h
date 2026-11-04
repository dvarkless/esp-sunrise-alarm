#pragma once
#include <Arduino.h>
#include <SettingsGyver.h>

// Единая точка логирования: пишет и в Serial, и в веб-виджет Log (sets::Logger).
// logger — кольцевой буфер; содержимое целиком уходит в браузер по опросу onUpdate.
extern sets::Logger logger;

// Форматный вывод (как printf); пишет в Serial и в logger.
// Префикс app_ — чтобы не конфликтовать с int log_printf() из esp32-hal-log.h.
void app_log_printf(const char* fmt, ...);

// print — зеркалируют Serial.print
inline void log_print(const char* s)                { Serial.print(s);   logger.print(s); }
inline void log_print(const __FlashStringHelper* s) { Serial.print(s);   logger.print(s); }
inline void log_print(char c)                       { Serial.print(c);   logger.print(c); }
inline void log_print(const Printable& p)           { Serial.print(p);   logger.print(p); }

// println — зеркалируют Serial.println
inline void log_println()                           { Serial.println();  logger.println(); }
inline void log_println(const char* s)              { Serial.println(s); logger.println(s); }
inline void log_println(const __FlashStringHelper* s) { Serial.println(s); logger.println(s); }
inline void log_println(const Printable& p)         { Serial.println(p); logger.println(p); }
