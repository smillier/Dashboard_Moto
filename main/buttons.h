#ifndef BUTTONS_H
#define BUTTONS_H

/**
 * Configures GPIO2 (shutter start/stop) as a debounced input and starts the
 * task that reacts to presses. Call once from app_main().
 *
 * Power, mode-cycle and reset used to be separate physical buttons on
 * GPIO3/4/18; that hardware was dropped in favor of the Waveshare LCD HAT
 * (see hat_input.h) - reset now lives on the HAT's KEY1, and power/mode
 * have no physical control for now.
 *
 * Wiring: the button goes between GPIO2 and GND. Internal pull-up is
 * enabled in software, so no external resistor is needed.
 */
#ifdef __cplusplus
extern "C" {
#endif

void buttons_init(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTONS_H
