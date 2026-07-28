#pragma once

#include "app_state.h"
#include "racebox_state.h"

// Drives a 2-pixel WS2812 addressable RGB bar: pixel 0 reflects the RaceBox
// bridge state, pixel 1 reflects the GoPro connection/recording state.
// Solid colors for steady states, blinking for transitional/error states.
// tick() must be called periodically (e.g. every 10ms) to advance blink
// timing for both pixels and push them out over the wire.
namespace status_led {

void init(int gpio_num, int led_count);
void set_racebox_state(RaceBoxState state);
void set_gopro_state(conn_state_t conn, bool recording);
void tick();

} // namespace status_led
