#include "light.h"
#include "config.h"

static bool     fading   = false;  // идёт ли розжиг
static float    acc      = 0.0f;   // накопленная яркость (float для плавности)
static uint32_t lastStep = 0;      // время последнего шага ШИМ

void light_setup() {
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_LED, PWM_CHANNEL);
    light_stop();
}

void light_startFadeIn() {
    acc = 0.0f;
    fading = true;
    lastStep = millis();
    ledcWrite(PWM_CHANNEL, 0);
}

void light_stop() {
    fading = false;
    acc = 0.0f;
    ledcWrite(PWM_CHANNEL, 0);
}

bool light_active() {
    return fading;
}

void light_update() {
    if (!fading) return;
    if (millis() - lastStep < FADE_STEP_MS) return;
    lastStep = millis();

    // Длительность розжига разбивается на равные шаги по FADE_STEP_MS
    uint32_t dur = config.fadeDuration ? config.fadeDuration : 1;
    uint32_t steps = dur / FADE_STEP_MS;
    if (!steps) steps = 1;

    acc += 255.0f / steps;
    uint32_t val = (uint32_t)acc;
    if (val > 255) val = 255;

    ledcWrite(PWM_CHANNEL, val);  // при PWM_RES = 8 допустим диапазон 0..255

    if (val >= 255) fading = false;  // розжиг завершён
}
