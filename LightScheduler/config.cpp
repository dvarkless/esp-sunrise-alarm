#include "config.h"
#include "log.h"
#include "sensors.h"

#include <LittleFS.h>
#include <SaverFile.h>

// Глобальный конфиг (значения по умолчанию заданы в структуре Config)
Config config;

// Saver: следит за изменениями Config и сохраняет его в /config.cfg.
//  'D'  — версия бинарного формата. 'D': добавлено поле maxBrightness (максимальная
//         яркость, %), оно вставлено после snoozeMinutes, поэтому раскладка Config
//         изменилась: массив days и последующие поля сдвинулись на байт. Размер при
//         этом остался 32 байта (сдвиг поглотил прежний байт выравнивания перед
//         fadeDuration), но старые 'C'/'B' /config.cfg несовместимы, поэтому
//         игнорируются: SaverFile при несовпадении версии в заголовке не читает
//         данные, а сразу перезаписывает файл значениями по умолчанию (одноразовый
//         сброс настроек при обновлении прошивки). При следующих несовместимых
//         изменениях структуры поднимайте букву;
//  10   — таймаут автосохранения, с (запись через ~5-10 с после последнего изменения);
//  FileStore::Atomic — атомарная замена файла (защита от сбоя питания).
SaverFile saver(LittleFS, "/config.cfg", config, 'D', 10, FileStore::Atomic);

void config_setup() {
#ifdef LS_BOARD_ESP32
    // true = отформатировать LittleFS при первом запуске, если ФС отсутствует
    LittleFS.begin(true);
#else
    // ESP8266: у FS::begin() нет аргумента formatOnFail — автоформат при сбое
    // монтирования включён в LittleFSConfig по умолчанию (autoFormat = true).
    LittleFS.begin();
#endif
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
