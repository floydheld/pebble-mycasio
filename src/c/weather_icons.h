#pragma once
#include <pebble.h>

// All drawing functions draw into a WEATHER_ICON_SIZE x WEATHER_ICON_SIZE layer.
#define WEATHER_ICON_SIZE 56

// icon: Climacons character code as sent by the JS (e.g. '!' = cloud, '$' = rain, 'I' = sun)
void weather_icon_draw(GContext *ctx, int icon, GColor fg, GColor bg);
// phase: 0 (new moon) .. 14 (full moon) .. 27
void moon_phase_draw(GContext *ctx, int phase, GColor lit, GColor dark);
// count: 0..4 wind lines in color, with an outline in outline color so they stay readable over the icon
void wind_lines_draw(GContext *ctx, int count, GColor color, GColor outline);
// striked through bluetooth logo
void bt_disconnected_draw(GContext *ctx, GColor fg, GColor strike);
