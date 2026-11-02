#include "button.h"
#include "config.h"

#include <EncButton.h>

// Кнопка к GND: режим INPUT_PULLUP, активный уровень LOW (по умолчанию в Button)
Button btn;

void button_setup() {
    btn.init(PIN_BUTTON);      // INPUT_PULLUP, LOW
    btn.setHoldTimeout(2000);  // удержание (пропуск сегодня) — 2 с; умолч. 600, макс. 4000 мс
}

void button_tick() {
    btn.tick();
}

bool button_click() {
    return btn.click();
}

bool button_hold() {
    return btn.hold();
}
