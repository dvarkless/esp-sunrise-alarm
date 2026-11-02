#include "button.h"
#include "config.h"

#include <EncButton.h>

// Кнопка к GND: режим INPUT_PULLUP, активный уровень LOW (по умолчанию в Button)
Button btn;

void button_setup() {
    btn.init(PIN_BUTTON);  // INPUT_PULLUP, LOW
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
