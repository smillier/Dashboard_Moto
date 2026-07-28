#ifndef BUTTONS_H
#define BUTTONS_H

/**
 * Configures GPIO2 (shutter start/stop), GPIO3 (power/sleep), GPIO4
 * (mode: Video/Photo/Timelapse) and GPIO18 (reset) as debounced
 * inputs and starts the task that reacts to presses. Call once from
 * app_main().
 *
 * Wiring: each button goes between its GPIO and GND. Internal
 * pull-ups are enabled in software, so no external resistors needed.
 */
#ifdef __cplusplus
extern "C" {
#endif

void buttons_init(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTONS_H
