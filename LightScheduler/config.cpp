#include "config.h"

#include <LittleFS.h>
#include <SaverFile.h>

// Глобальный конфиг (значения по умолчанию заданы в структуре Config)
Config config;

// Saver: следит за изменениями Config и сохраняет его в /config.cfg.
//  'B'  — версия бинарного формата. 'B': расписание переведено на дни недели
//         (days[7], snoozeMinutes), структура несовместима с 'A', поэтому старый
//         /config.cfg игнорируется: SaverFile при несовпадении версии/размера в заголовке
//         не читает данные, а сразу перезаписывает файл значениями по умолчанию
//         (одноразовый сброс настроек при обновлении прошивки). При следующих
//         несовместимых изменениях структуры поднимайте букву;
//  10   — таймаут автосохранения, с (запись через ~5-10 с после последнего изменения);
//  FileStore::Atomic — атомарная замена файла (защита от сбоя питания).
SaverFile saver(LittleFS, "/config.cfg", config, 'B', 10, FileStore::Atomic);

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
