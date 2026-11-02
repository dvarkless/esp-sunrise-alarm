#include "config.h"

#include <LittleFS.h>
#include <SaverFile.h>

// Глобальный конфиг (значения по умолчанию заданы в структуре Config)
Config config;

// Saver: следит за изменениями Config и сохраняет его в /config.cfg.
//  'A'  — версия бинарного формата (меняйте при несовместимых изменениях структуры);
//  10   — таймаут автосохранения, с (запись через ~5-10 с после последнего изменения);
//  FileStore::Atomic — атомарная замена файла (защита от сбоя питания).
SaverFile saver(LittleFS, "/config.cfg", config, 'A', 10, FileStore::Atomic);

void config_setup() {
    // true = отформатировать LittleFS при первом запуске, если ФС отсутствует
    LittleFS.begin(true);
    saver.begin();
}

void config_tick() {
    saver.tick();
}

void config_save() {
    saver.write();
}
