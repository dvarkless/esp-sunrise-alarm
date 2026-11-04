#include "config.h"

#include <LittleFS.h>
#include <SaverFile.h>
#include "log.h"
#include "sensors.h"

// Глобальный конфиг (значения по умолчанию заданы в структуре Config)
Config config;

// Saver: следит за изменениями Config и сохраняет его в /config.cfg.
//  'C'  — версия бинарного формата. 'C': fadeDuration переведён в минуты и добавлен
//         turnOffDelay, структура Config выросла с 28 до 32 байт. Старый 'B'
//         /config.cfg несовместим, поэтому игнорируется: SaverFile при несовпадении
//         версии/размера в заголовке не читает данные, а сразу перезаписывает файл
//         значениями по умолчанию (одноразовый сброс настроек при обновлении прошивки).
//         При следующих несовместимых изменениях структуры поднимайте букву;
//  10   — таймаут автосохранения, с (запись через ~5-10 с после последнего изменения);
//  FileStore::Atomic — атомарная замена файла (защита от сбоя питания).
SaverFile saver(LittleFS, "/config.cfg", config, 'C', 10, FileStore::Atomic);

void config_setup() {
    // true = отформатировать LittleFS при первом запуске, если ФС отсутствует
    LittleFS.begin(true);
    saver.begin();
}

void config_tick() {
    if (saver.tick() == Saver::Status::Write) {
        log_print(F("Настройки сохранены: "));
        log_print(sensors_date_str().c_str());
        log_print(' ');
        log_println(sensors_time_str().c_str());
    }
}

void config_save() {
    if (saver.write() == Saver::Status::Write) {
        log_print(F("Настройки сохранены: "));
        log_print(sensors_date_str().c_str());
        log_print(' ');
        log_println(sensors_time_str().c_str());
    }
}
