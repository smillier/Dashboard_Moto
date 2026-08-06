#ifndef HAT_INPUT_H
#define HAT_INPUT_H

/**
 * Configures the Waveshare 1.44" LCD HAT's 5-way joystick (up/down/left/
 * right/press) and KEY1/2/3 as debounced inputs and starts the task that
 * reacts to presses. Call once from app_main(), after buttons_init() (both
 * share the GPIO ISR service, installed once by whichever runs first).
 *
 * Only KEY1 is bound to an action right now (device reset, replacing the
 * old GPIO18 physical button). The joystick directions and KEY2/KEY3 are
 * wired, debounced and logged but otherwise reserved for a future
 * on-screen menu.
 *
 * Wiring: each switch goes between its GPIO and GND, same as the HAT's
 * own Raspberry Pi reference wiring. Internal pull-ups are enabled in
 * software, so no external resistors are needed.
 */
#ifdef __cplusplus
extern "C" {
#endif

void hat_input_init(void);

#ifdef __cplusplus
}
#endif

#endif // HAT_INPUT_H
