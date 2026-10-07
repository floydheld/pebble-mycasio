#pragma once
#include <pebble.h>  

typedef void effect_cb(GContext* ctx, GRect position, void* param);

// inverter effect with a given color for the bright color (switch between black / color).
// Added by FG
effect_cb effect_invert_color;
uint8_t GlobalInverterColor; //this color is used if effect was added with parameter of 0. In this way, the color can be changed later.
uint8_t GlobalBkgColor;
