#include "webui.h"
#include "config.h"
#include "light.h"
#include "sensors.h"
#include "log.h"

#include <WiFi.h>
#include <SettingsGyver.h>

SettingsGyver sett("Light Scheduler");

// ID виджетов датчиков — по ним webui обновляет значения в браузере
enum : size_t {
    ID_TEMP = 1,
    ID_HUM  = 2,
    ID_PRESS = 3,
    ID_LOG = 4,
};

// Имена дней недели для подписей: индекс 0=Пн ... 6=Вс (ISO 8601)
static const char* const DAY_NAMES[7] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};

// Билдер: собирает веб-интерфейс. Поля Config подключены по указателям,
// поэтому изменения из браузера сразу пишутся в config, а Saver сохраняет их.
void build(sets::Builder& b) {
    {
        sets::Group g(b, "Расписание");
        b.Switch("Расписание включено", &config.enabled);

        // 7 независимых дней: каждый день — Row внутри группы, а не вложенная группа
        // (Settings 1.4.3 молча отбрасывает вложенные группы, а строки внутри группы
        // рендерятся нормально). Settings адресует виджеты по label, поэтому подписи
        // уникальны по всей странице (день в скобках).
        // Изменения сразу пишутся в config.days[i], Saver сохранит их автоматически.
        char label[32];
        for (uint8_t i = 0; i < 7; i++) {
            sets::Row day(b, DAY_NAMES[i]);
            snprintf(label, sizeof(label), "Включено (%s)", DAY_NAMES[i]);
            b.Switch(label, &config.days[i].enabled);
            snprintf(label, sizeof(label), "Часы (%s)", DAY_NAMES[i]);
            b.Spinner(label, 0, 23, 1, &config.days[i].hour);
            snprintf(label, sizeof(label), "Минуты (%s)", DAY_NAMES[i]);
            b.Spinner(label, 0, 59, 1, &config.days[i].minute);
        }
    }

    {
        sets::Group g(b, "Розжиг");
        b.Slider("Длительность (мин)", 0, 60, 1, "мин", &config.fadeDuration);
        if (b.Button("Запуск будильника")) {
            light_startFadeIn();
        }
        if (b.Button("Выключить будильник")) {
            light_stop();
        }
    }

    {
        sets::Group g(b, "Кнопка");
        // Клик по кнопке откладывает розжиг на это время; Saver сохранит значение сам
        b.Slider("Отложить (мин)", 1, 60, 1, "мин", &config.snoozeMinutes);
        b.Slider("Время свечения (мин)", 0, 60, 1, "мин", &config.turnOffDelay);
    }

    {
        sets::Group g(b, "Датчики");
        b.LabelFloat(ID_TEMP,  "Температура (°C)", sensors_temperature(), 1);
        b.LabelFloat(ID_HUM,   "Влажность (%)",    sensors_humidity(),    1);
        b.LabelFloat(ID_PRESS, "Давление (гПа)",   sensors_pressure(),    1);
    }

    // Виджет-лог: сюда уходит всё, что пишется через log_*() (и в Serial). Последний элемент страницы.
    b.Log(ID_LOG, logger, "Лог");
}

// Вызывается браузером периодически (режим без WebSocket) — отдаём свежие показания датчиков
void update(sets::Updater& upd) {
    upd.update(ID_TEMP,  sensors_temperature());
    upd.update(ID_HUM,   sensors_humidity());
    upd.update(ID_PRESS, sensors_pressure());
    upd.update(ID_LOG, logger);
}

// Подключение к WiFi. Если SSID не задан — поднимаем точку доступа для настройки/отладки.
static void wifiConnect() {
    WiFi.mode(WIFI_STA);

    if (WIFI_SSID[0]) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        log_print(F("WiFi: подключение"));
        uint32_t tmr = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - tmr < 15000UL) {
            delay(500);
            log_print('.');
        }
        log_println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        log_print(F("WiFi: подключено, IP "));
        log_println(WiFi.localIP());
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_NAME);
        log_print(F("WiFi: точка доступа " AP_NAME ", IP "));
        log_println(WiFi.softAPIP());
    }
}

void webui_setup() {
    wifiConnect();

    sett.begin();
    sett.onBuild(build);
    sett.onUpdate(update);
}

void webui_tick() {
    sett.tick();
}
