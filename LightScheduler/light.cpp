#include "light.h"
#include "config.h"

// ===================== Перцептуальная кривая розжига =====================
// Формула: d(p) = p^γ, γ = ln(0.30)/ln(0.50) = 1.7369655941662063
// (50 % прогресса розжига дают 30 % скважности ШИМ).
// Таблица сгенерирована offline (Python, округление half-up): индекс — прогресс
// 0..255, значение — скважность ШИМ 0..255 при PWM_RES = 8. Рантайм — только целые.
// Обоснование, сравнение моделей восприятия, генератор и проверки — docs/light-curve.md.
static const uint8_t CURVE_LUT[256] = {
    //  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
       0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  2,  2,  //   0..15
       2,  2,  3,  3,  3,  3,  4,  4,  4,  5,  5,  5,  5,  6,  6,  7,  //  16..31
       7,  7,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14,  //  32..47
      14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 21, 21, 22, 22,  //  48..63
      23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32, 33, 33,  //  64..79
      34, 35, 36, 36, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45, 46,  //  80..95
      47, 48, 48, 49, 50, 51, 52, 53, 54, 55, 56, 56, 57, 58, 59, 60,  //  96..111
      61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,  // 112..127
      77, 78, 79, 80, 81, 82, 83, 84, 86, 87, 88, 89, 90, 91, 92, 93,  // 128..143
      95, 96, 97, 98, 99,100,101,103,104,105,106,107,109,110,111,112,  // 144..159
     113,115,116,117,118,120,121,122,124,125,126,127,129,130,131,133,  // 160..175
     134,135,137,138,139,141,142,143,145,146,147,149,150,152,153,154,  // 176..191
     156,157,159,160,161,163,164,166,167,169,170,172,173,175,176,178,  // 192..207
     179,180,182,184,185,187,188,190,191,193,194,196,197,199,200,202,  // 208..223
     204,205,207,208,210,212,213,215,216,218,220,221,223,225,226,228,  // 224..239
     230,231,233,235,236,238,240,241,243,245,246,248,250,252,253,255,  // 240..255
};
static_assert(sizeof(CURVE_LUT) == 256, "CURVE_LUT: ожидается 256 байт");

static bool     fading    = false;  // идёт ли розжиг (light_active)
static bool     on        = false;  // свет включён: true от light_startFadeIn до light_stop
static uint32_t fadeStart = 0;      // millis() начала розжига
static uint32_t lastStep  = 0;      // millis() последнего шага ШИМ

void light_setup() {
    ledcAttachChannel(PIN_PWM_LED, PWM_FREQ, PWM_RES, PWM_CHANNEL);
    light_stop();
}

void light_startFadeIn() {
    on = true;  // свет считается включённым сразу, ещё до первого шага ШИМ
    fading = true;
    fadeStart = millis();
    lastStep = fadeStart;
    ledcWriteChannel(PWM_CHANNEL, 0);
}

void light_stop() {
    on = false;
    fading = false;
    ledcWriteChannel(PWM_CHANNEL, 0);
}

bool light_active() {
    return fading;
}

bool light_isOn() {
    return on;
}

void light_update() {
    if (millis() - lastStep < FADE_STEP_MS) return;
    lastStep = millis();

    uint32_t elapsed = millis() - fadeStart;
    uint32_t dur = fading ? config.fadeDuration * 60 * 1000 : config.turnOffDelay * 60 * 1000;

    // dur до 3 600 000 мс (60 мин) и elapsed < dur => elapsed*255 < ~918 млн (умещается
    // в uint32), idx <= 254; 255 выставляется отдельной веткой выше. Накопления нет — дрейфа нет.
    if (fading) {
        if (!dur || elapsed >= dur) {
            ledcWriteChannel(PWM_CHANNEL, 255);
            fading = false;
            fadeStart = millis();
            return;
        }
        uint16_t idx = (uint16_t)(elapsed * 255UL / dur);
        ledcWriteChannel(PWM_CHANNEL, CURVE_LUT[idx]);  // при PWM_RES = 8 допустим диапазон 0..255
    }
    else {
        if (!dur || elapsed >= dur) {
            light_stop();
            return;
        }
    }
}
