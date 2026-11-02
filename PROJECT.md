# Smart Scheduled Light Controller (ESP32)

Проект **esp-sunrise-alarm**: устройство на ESP32, которое плавно включает свет в заданное время по часам DS3231, показывает температуру/влажность/давление с BME280 в веб-интерфейсе и управляется физической кнопкой.

Документ описывает **фактическое** состояние репозитория: структуру файлов, модули прошивки, пины, библиотеки и поведение кода.

---

## Структура репозитория

Ардуино-скетч лежит целиком в папке `LightScheduler/` — это плоская раскладка, обязательная для Arduino IDE (все `.h/.cpp` должны находиться рядом со скетчем).

```text
esp-sunrise-alarm/
├── .gitignore              // шаблон для C/C++ артефактов сборки (не Arduino-специфичный)
├── LICENSE                 // MIT, Copyright (c) 2026 Amir Suleymanov
├── README.md               // заголовок и одна строка описания
├── PROJECT.md              // этот файл
└── LightScheduler/         // прошивка (скетч + модули)
    ├── LightScheduler.ino  // setup() + неблокирующий loop(), логика расписания
    ├── config.h            // пины, PWM, I2C, WiFi, struct Config, прототипы
    ├── config.cpp          // глобальный config + Saver поверх LittleFS
    ├── sensors.h           // API датчиков и RTC
    ├── sensors.cpp         // Wire, BME280, DS3231, кэш измерений
    ├── light.h             // API плавного розжига
    ├── light.cpp           // LEDC (PWM) и неблокирующий fade
    ├── button.h            // API кнопки
    ├── button.cpp          // EncButton: клик и удержание
    ├── webui.h             // API веб-интерфейса
    ├── webui.cpp           // WiFi + SettingsGyver (build/update)
    └── README.md           // краткая инструкция по библиотекам и запуску
```

Особенности, которых в репозитории **нет**: `platformio.ini`, CMake, CI, тестов, скриптов сборки, файлов LittleFS-образа. Сборка — только через Arduino IDE (или arduino-cli) по скетчу `LightScheduler.ino`.

---

## Аппаратное обеспечение

| Компонент | Назначение |
|---|---|
| ESP32 Dev Board | Основной контроллер |
| PWM LED-контроллер (MOSFET/драйвер) | Управление яркостью ленты/лампы |
| BME280 | Температура, влажность, давление |
| DS3231 (RTC) | Часы реального времени с батарейной подпиткой |
| Тактовая кнопка | Ручное управление |
| Блок питания 3.3V / 5V | Питание платы и периферии |

### Пины (из [`config.h`](LightScheduler/config.h))

| Определение | Значение | Подключение |
|---|---|---|
| `PIN_PWM_LED` | GPIO 5 | Вход MOSFET/драйвера, общий GND с ESP32 |
| `PIN_I2C_SDA` | GPIO 21 | Общая шина BME280 + DS3231 |
| `PIN_I2C_SCL` | GPIO 22 | Общая шина BME280 + DS3231 |
| `PIN_BUTTON` | GPIO 27 | Второй вывод на GND, `INPUT_PULLUP` |

Адреса на общей шине I2C: `BME280_ADDR = 0x76` (альтернатива `0x77`), `RTC_ADDR = 0x68` (константа носит справочный характер: `rtc.begin()` из GyverDS3231 обращается к адресу по умолчанию).

### Прочие константы

| Константа | Значение | Смысл |
|---|---|---|
| `PWM_FREQ` | 5000 Гц | Частота LEDC |
| `PWM_RES` | 8 бит | Диапазон скважности 0…255 |
| `PWM_CHANNEL` | 0 | Канал LEDC |
| `FADE_STEP_MS` | 50 мс | Период шага розжига |
| `TIME_ZONE` | 3 | Часовой пояс для DS3231 (`setStampZone`) |
| `WIFI_SSID` / `WIFI_PASS` | `""` / `""` | Учётные данные сети; пустой SSID → точка доступа |
| `AP_NAME` | `"LightScheduler"` | Имя точки доступа |

---

## Программная архитектура

Главный файл содержит только `setup()` и неблокирующий `loop()`, который опрашивает тикеры модулей. Модули общаются через простые функции из `.h`-файлов; общего класса-синглтона или событийной шины нет.

```text
                     ┌──────────────────────────┐
                     │  LightScheduler.ino      │
                     │  loop(): тикеры + распис.│
                     └───┬───────┬───────┬──────┘
        config_tick() ───┘       │       └─── webui_tick()
                                 │
        ┌────────────┬───────────┼───────────┬────────────┐
        ▼            ▼           ▼           ▼            ▼
   ┌─────────┐ ┌──────────┐ ┌────────┐ ┌─────────┐ ┌──────────┐
   │ config  │ │ sensors  │ │ light  │ │ button  │ │  webui   │
   │+ Saver  │ │BME+DS3231│ │ LEDC   │ │EncButton│ │ Settings │
   └────┬────┘ └──────────┘ └────────┘ └─────────┘ └──────────┘
        │                     ▲            │             │
        │ изменяет config     └────────────┴─────────────┘
        └──────────► /config.cfg (LittleFS, Atomic)
```

### `config` — [`config.h`](LightScheduler/config.h) / [`config.cpp`](LightScheduler/config.cpp)

Единый источник правды для настроек:

```cpp
struct Config {
    uint8_t  scheduleHour = 7;      // 0-23
    uint8_t  scheduleMin  = 30;     // 0-59
    bool     enabled      = true;   // расписание активно
    uint32_t fadeDuration = 30000;  // мс, 0-60000
};
extern Config config;
```

Функции: `config_setup()` (`LittleFS.begin(true)` + `saver.begin()`), `config_tick()` (`saver.tick()`), `config_save()` (`saver.write()`).

Сохранение: `SaverFile saver(LittleFS, "/config.cfg", config, 'A', 10, FileStore::Atomic)` — версия формата `'A'`, автосохранение через 10 с после последнего изменения, атомарная замена файла (защита от сбоя питания).

### `sensors` — [`sensors.h`](LightScheduler/sensors.h) / [`sensors.cpp`](LightScheduler/sensors.cpp)

| Функция | Поведение |
|---|---|
| `sensors_setup()` | `Wire.begin(SDA, SCL)`, `setStampZone(TIME_ZONE)`, `rtc.begin()`; при `rtc.isReset()` ставит время компиляции (`setBuildTime()`); `bme.begin(BME280_ADDR)` |
| `sensors_tick()` | Возвращает `true` раз в секунду (`rtc.tick()`); при этом обновляет кэш измерений BME280 |
| `sensors_ok()` | Флаг успешной инициализации BME280 |
| `sensors_temperature()` / `sensors_humidity()` / `sensors_pressure()` | Кэшированные значения; давление пересчитано из Па в гПа (`/100.0f`) |
| `sensors_hour()` / `sensors_minute()` | Текущее время по DS3231 |

### `light` — [`light.h`](LightScheduler/light.h) / [`light.cpp`](LightScheduler/light.cpp)

| Функция | Поведение |
|---|---|
| `light_setup()` | `ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES)` + `ledcAttachPin` + гашение |
| `light_startFadeIn()` | Сброс накопленной яркости, `ledcWrite(0)`, запуск fade |
| `light_stop()` | Мгновенное гашение и остановка fade |
| `light_update()` | Неблокирующий шаг: не чаще `FADE_STEP_MS`, `acc += 255 / (fadeDuration / FADE_STEP_MS)` |
| `light_active()` | `true`, пока идёт розжиг |

### `button` — [`button.h`](LightScheduler/button.h) / [`button.cpp`](LightScheduler/button.cpp)

Обёртка над `Button` из EncButton: `button_setup()` (`btn.init(PIN_BUTTON)`), `button_tick()`, `button_click()`, `button_hold()`. Порог удержания не переопределяется и берётся по умолчанию из библиотеки.

### `webui` — [`webui.h`](LightScheduler/webui.h) / [`webui.cpp`](LightScheduler/webui.cpp)

| Функция | Поведение |
|---|---|
| `webui_setup()` | `wifiConnect()`, затем `sett.begin()` + регистрация `onBuild(build)` и `onUpdate(update)` |
| `webui_tick()` | `sett.tick()` |

Wi-Fi: при заданном `WIFI_SSID` — режим STA с ожиданием до 15 с (блокирующий `delay(500)` **внутри `setup()`**, не в `loop()`); иначе или при неудаче — режим AP с именем `AP_NAME` (IP по умолчанию `192.168.4.1`). IP выводится в Serial.

---

## Логика работы

1. **`setup()`** вызывает `config_setup → sensors_setup → light_setup → button_setup → webui_setup`.
2. **`loop()`** (без блокировок): `config_tick()`, `light_update()`, `button_tick()`, `webui_tick()`.
3. **Кнопка**: `button_click()` → `light_startFadeIn()`; `button_hold()` → `light_stop()` и сброс флага `alarmTriggered`.
4. **Расписание** — раз в секунду, по `sensors_tick()`:
   - состояние хранится в `.ino`: `alarmTriggered` (сработало в текущую минуту) и `lastMinute` (последняя обработанная минута);
   - при смене минуты `alarmTriggered` сбрасывается — это позволяет будильнику срабатывать ежедневно;
   - если `config.enabled && !alarmTriggered && hour == config.scheduleHour && minute == config.scheduleMin` → `light_startFadeIn()` + `alarmTriggered = true` (защита от повтора в ту же минуту).
5. **Розжиг**: `light_update()` раз в 50 мс поднимает скважность 0→255 за `config.fadeDuration` и по достижении 255 останавливается.

### Нюансы реализации

- `fadeDuration = 0` трактуется как 1 мс → один шаг, свет сразу на максимуме; значения меньше `FADE_STEP_MS` фактически дают один шаг.
- Удержание кнопки вызывает `light_stop()`, то есть прерывает и текущий ручной розжиг.
- `light_stop()` не сбрасывает `alarmTriggered` (кроме вызова из обработчика удержания в `loop()`); веб-кнопка «Розжечь сейчас» флаг не трогает.
- Время RTC устанавливается из времени компиляции только при `rtc.isReset()`; NTP-синхронизации нет.
- SSID/пароль Wi-Fi в коде константами — отдельного экрана настройки сети нет.

---

## Веб-интерфейс

Поднимается библиотекой **Settings** (класс `SettingsGyver`, заголовок `<SettingsGyver.h>`), сервер на порту 80. Открывается по IP устройства, выведенному в Serial (`192.168.4.1` в режиме точки доступа). Виджеты подключены к полям `config` по указателям, поэтому правки из браузера сразу меняют структуру, а Saver затем сохраняет её в `/config.cfg`.

| Группа | Виджет | Поле / действие |
|---|---|---|
| Расписание | `Spinner` «Час», 0…23, шаг 1 | `config.scheduleHour` |
| Расписание | `Spinner` «Минута», 0…59, шаг 1 | `config.scheduleMin` |
| Расписание | `Switch` «Включено» | `config.enabled` |
| Розжиг | `Slider` «Длительность (мс)», 0…60000, шаг 1000, единица `ms` | `config.fadeDuration` |
| Розжиг | `Button` «Розжечь сейчас» | вызов `light_startFadeIn()` |
| Датчики | `LabelFloat` «Температура (°C)» / «Влажность (%)» / «Давление (гПа)», 1 знак | ID 1/2/3, только чтение |

Показания датчиков обновляются через `onUpdate(update)` — режим периодического опроса, без WebSocket.

---

## Библиотеки

| Библиотека | Заголовок / класс | Назначение |
|---|---|---|
| **Settings** | `<SettingsGyver.h>`, `SettingsGyver` | Веб-интерфейс и генерация HTML/JS |
| **Saver** | `<SaverFile.h>`, `SaverFile` | Хранение `Config` в LittleFS, режим `FileStore::Atomic` |
| **EncButton** | `<EncButton.h>`, `Button` | Клик и удержание, антидребезг |
| **GyverBME280** | `<GyverBME280.h>`, `GyverBME280` | Драйвер BME280 |
| **GyverDS3231** | `<GyverDS3231.h>`, `GyverDS3231` | Драйвер DS3231, `setStampZone`, `setBuildTime` |
| ESP32 core | `<Wire.h>`, `<WiFi.h>`, `<LittleFS.h>`, `ledc*` | I2C, Wi-Fi, ФС, ШИМ |

Транзитивные зависимости (ставятся автоматически менеджером библиотек): GTL, GyverDB, StringUtils, GyverHTTP, BSON, Stamp, Table, StreamIO (для Settings), GyverIO (для EncButton), Stamp (для GyverDS3231).

> **Про имя библиотеки:** в старых материалах она называется **SettingsGyver**; сейчас пакет в менеджере библиотек называется **Settings**, а класс внутри остался `SettingsGyver` — именно он и подключается в [`webui.cpp`](LightScheduler/webui.cpp).

---

## Сборка и прошивка

1. Установить **esp32 by Espressif Systems** (Boards Manager) и выбрать плату **ESP32 Dev Module**.
2. Установить библиотеки из таблицы выше (`Sketch → Include Library → Manage Libraries`).
3. `Tools → Partition Scheme` — схема с LittleFS, например **Default 4MB with spiffs** (нужно место под `/config.cfg`).
4. Открыть [`LightScheduler.ino`](LightScheduler/LightScheduler.ino); все `.h/.cpp` уже лежат в той же папке.
5. Прошить, открыть Serial Monitor на 115200 — в логе будут версия, статусы датчиков, RTC и IP-адрес веб-интерфейса.

Настройка под свою схему — константы и значения по умолчанию в [`config.h`](LightScheduler/config.h), Wi-Fi — `WIFI_SSID`/`WIFI_PASS`/`AP_NAME` там же.

---

## Возможные расширения

- Deep Sleep с пробуждением по SQW DS3231 для экономии энергии.
- NTP-синхронизация DS3231 при подключении к Wi-Fi.
- Массив расписаний в `Config` (будни/выходные) вместо одного времени.
- «Снуз»: удержание кнопки откладывает розжиг на 5/10/15 минут.
- История показаний датчиков в LittleFS.
- Перенос сборки на PlatformIO и добавление CI-проверки компиляции.

---

## Лицензия

MIT — см. [`LICENSE`](LICENSE). Библиотеки AlexGyver распространяются по MIT в соответствующих репозиториях.
