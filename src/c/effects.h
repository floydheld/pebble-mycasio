#pragma once
#include <pebble.h>  

typedef void effect_cb(GContext* ctx, GRect position, void* param);

// inverter effect: the background color becomes the given color, any other color the background color.
// Added by FG
// Parameter: uint8_t[2] = {color, background color} (argb & 0b00111111), can be changed later
effect_cb effect_invert_color;
