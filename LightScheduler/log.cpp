#include "log.h"

#include <stdarg.h>
#include <stdio.h>

// Кольцевой буфер логов: последние ~1 КБ текста видны в браузере (виджет Log).
sets::Logger logger(1024);

void app_log_printf(const char* fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    logger.print(buf);
}
