#include "webui.h"
#include "config.h"
#include "light.h"
#include "sensors.h"

#include <WiFi.h>
#include <SettingsGyver.h>

SettingsGyver sett("Light Scheduler");

// ID виджетов датчиков — по ним webui обновляет значения в браузере
enum : size_t {
    ID_TEMP = 1,
    ID_HUM  = 2,
    ID_PRESS = 3,
};

// Билдер: собирает веб-интерфейс. Поля Config подключены по указателям,
// поэтому изменения из браузера сразу пишутся в config, а Saver сохраняет их.
void build(sets::Builder& b) {
    {
        sets::Group g(b, "Расписание");
        b.Spinner("Час", 0, 23, 1, &config.scheduleHour);
        b.Spinner("Минута", 0, 59, 1, &config.scheduleMin);
        b.Switch("Включено", &config.enabled);
    }

    {
        sets::Group g(b, "Розжиг");
        b.Slider("Длительность (мс)", 0, 60000, 1000, "ms", &config.fadeDuration);
        if (b.Button("Розжечь сейчас")) {
            light_startFadeIn();
        }
    }

    {
        sets::Group g(b, "Датчики");
        b.LabelFloat(ID_TEMP,  "Температура (°C)", sensors_temperature(), 1);
        b.LabelFloat(ID_HUM,   "Влажность (%)",    sensors_humidity(),    1);
        b.LabelFloat(ID_PRESS, "Давление (гПа)",   sensors_pressure(),    1);
    }
}

// Вызывается браузером периодически (режим без WebSocket) — отдаём свежие показания датчиков
void update(sets::Updater& upd) {
    upd.update(ID_TEMP,  sensors_temperature());
    upd.update(ID_HUM,   sensors_humidity());
    upd.update(ID_PRESS, sensors_pressure());
}

// Подключение к WiFi. Если SSID не задан — поднимаем точку доступа для настройки/отладки.
static void wifiConnect() {
    WiFi.mode(WIFI_STA);

    if (WIFI_SSID[0]) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Serial.print(F("WiFi: подключение"));
        uint32_t tmr = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - tmr < 15000UL) {
            delay(500);
            Serial.print('.');
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("WiFi: подключено, IP "));
        Serial.println(WiFi.localIP());
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_NAME);
        Serial.print(F("WiFi: точка доступа " AP_NAME ", IP "));
        Serial.println(WiFi.softAPIP());
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
