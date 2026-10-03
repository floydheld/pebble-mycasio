#pragma once

#include <pebble.h>

// size of the original pixel design of the digits, which is scaled to the digit size
#define SEVEN_SEGMENT_DESIGN_W 26
#define SEVEN_SEGMENT_DESIGN_H 41

// Paints a seven segment digit (0..9) filling bounds. Other values paint nothing.
void seven_segment_paint(GContext* ctx, int digit, GRect bounds);
