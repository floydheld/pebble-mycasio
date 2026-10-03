#include <pebble.h>
#include "config.h"
#include "keys.h"
#include "mooncalc.h"
#include "seven_segment.h"
#include "weather_icons.h"
#include "effect_layer.h"

//deploy:
/*
/mnt/d/0meine dateien/my progs/github/pebble-mycasio$ pebble install --phone 192.168.0.6
*/
// Pebble Time 2 (emery, 200x228)
// Rows from top to bottom: location | last update, weather condition, weather icon | next rain | temperature + min/max temperature, time, date, sleep (deep sleep) | battery
// Each row is as high as its content plus PADDING above and below. The weather condition takes the remaining space.
// Rows with the same background are separated by a 1px line.
#define PADDING 2
#define TIME_PADDING (PADDING + 2) // the time has 2px more

#define DIGIT_WIDTH     40
#define DIGIT_HEIGHT    66
#define COLON_SIZE      (7*DIGIT_WIDTH/SEVEN_SEGMENT_DESIGN_W) // the colon squares are 7 pixels in the original 26x41 design
#define DIGIT_GAP       6  // between the digits and to the colon
#define TIME_X          ((200 - 4*DIGIT_WIDTH - 4*DIGIT_GAP - COLON_SIZE)/2)
#define COLON_X         (TIME_X + 2*(DIGIT_WIDTH + DIGIT_GAP))
#define RAIN_WIDTH      48 // next rain indicator between weather icon and temperature

// measured glyph extents of the system fonts: top offset within the text layer and height (incl. descenders)
#define GOTHIC_18_TOP        6
#define GOTHIC_18_H          13
#define GOTHIC_18_BOLD_TOP   7
#define GOTHIC_18_BOLD_H     11
#define GOTHIC_24_BOLD_TOP   10
#define GOTHIC_24_BOLD_H     18
#define GOTHIC_24_BOLD_DESC  4    // descender part of the height
#define GOTHIC_28_BOLD_TOP   8    // digits
#define GOTHIC_28_BOLD_H     20
#define SLEEP_TOP            8    // GOTHIC_24_BOLD with parentheses
#define SLEEP_H              18
// custom fonts (DejaVu Sans Bold), measured the same way; condition: single line sizes 36, 30, 24 and two lines of 18
#define CONDITION_36_TOP     9
#define CONDITION_36_H       35
#define CONDITION_30_TOP     7
#define CONDITION_30_H       29
#define CONDITION_24_TOP     6
#define CONDITION_24_H       23
#define CONDITION_18_TOP     4
#define CONDITION_18_H       17
#define CONDITION_18_PITCH   18
#define TEMP_TOP             9    // size 36, digits and degree sign
#define TEMP_H               26
#define TEMP_MIN_MAX_TOP     1    // size 22, incl. the slash
#define TEMP_MIN_MAX_H       23

#define LOCATION_Y   0
#define LOCATION_H   (GOTHIC_18_BOLD_H + 2*PADDING)
#define CONDITION_Y  (LOCATION_Y + LOCATION_H + 1)
#define CONDITION_H  (WEATHER_Y - 1 - CONDITION_Y)
#define WEATHER_Y    (CLOCK_Y - 1 - WEATHER_ICON_SIZE)
#define CLOCK_Y      (DATE_Y - CLOCK_H)
#define CLOCK_H      (DIGIT_HEIGHT + 2*TIME_PADDING)
#define DATE_Y       (BOTTOM_Y - 1 - DATE_H)
#define DATE_H       (GOTHIC_24_BOLD_H + 2*PADDING)
#define BOTTOM_Y     (228 - BOTTOM_H)
#define BOTTOM_H     (BATTERY_HEIGHT + 2*PADDING) // the sleep text (SLEEP_H) is a bit lower than the battery
#define DIGIT_Y      (CLOCK_Y + TIME_PADDING)
#define BATTERY_Y    (BOTTOM_Y + PADDING)
#define BATTERY_HEIGHT 19
#define BATTERY_WIDTH  65 // body without the nub
#define BATTERY_BORDER 3  // gray border around the charged part
#define BATTERY_X      (195 - BATTERY_WIDTH) // the nub is right of the body up to 197

// y of a text layer, so that its glyphs (top offset, height) are vertically centered in the row (row_y, row_h)
#define TEXT_Y(row_y, row_h, top, h) ((row_y) + ((row_h) - (h))/2 - (top))

static const char *s_weekdays[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};

// Colors:
#define COLOR_HEADER_TEXT     GColorWhite      // location, last update
#define COLOR_HEADER_BKGR     GColorBlack
#define COLOR_CONDITION_TEXT  GColorWhite
#define COLOR_CONDITION_BKGR  GColorBlack
#define COLOR_DATE_TEXT       GColorOxfordBlue
#define COLOR_DATE_BKGR       GColorPastelYellow
#define COLOR_CLOCK           GColorWhite      // time
#define COLOR_CLOCK_BKGR      GColorBlack
#define COLOR_LINES           GColorDarkGray
#define COLOR_BATTERY_BKGR    GColorWhite      // the charged part is green, orange or red
#define COLOR_BATTERY_BORDER  GColorLightGray
#define COLOR_BT              GColorVividCerulean
#define COLOR_BT_BKGR         GColorBlack
#define COLOR_MOON_DARK       GColorDarkGray
#define COLOR_WIND            GColorDukeBlue
#define COLOR_WIND_OUTLINE    GColorBlack

static Window *s_main_window;
static Layer *s_window_layer;
static Layer *s_background_layer;
static Layer *s_digit_layers[4]; // each with an int as data: the digit to paint, -1 = none
static Layer *s_icon_layer;      // weather icon / moon phase / bluetooth disconnected, plus wind lines
static Layer *s_rain_layer;      // next rain indicator
static TextLayer *s_location_layer;
static TextLayer *s_last_update_layer;
static TextLayer *s_condition_layer;
static GFont s_condition_fonts[4]; // DejaVu Sans Bold 36, 30, 24, 18
static GFont s_temp_font;
static GFont s_temp_min_max_font;
static TextLayer *s_temp_layer;
static TextLayer *s_temp_min_max_layer;
static TextLayer *s_date_layer;
static TextLayer *s_sleep_layer;
static TextLayer *s_battery_text_layer;
static EffectLayer *s_battery_fill_layer; // inverts the battery (incl. its text) according to the charge level

// Weather data (persisted):
static char location_name[32];
static int  location_latitude  = (int)(LATITUDE*1E6); //in 1E6
static int  weather_temp       = 0; //in degree C
static int  weather_icon       = (int)'I';
static int  wind_speed         = 0; //in km/h
static int  rain_in            = -1; //next rain: -1 = none in sight, 0 = now, 1..23 = in hours (today), 100+d = in d days
static char temp_min_max[32];
static char weather_condition[32];
static time_t phone_last_updated = 0;
static time_t sun_rise_unix_loc = 0;
static time_t sun_set_unix_loc  = 0;
static int warning_location = 0; //0: no warning, 1: red warning (GPS differed from setting), 2: black warning (no GPS)
static int last_charge_state = 0; //0: discharging; 1: plugged & charging; 2: plugged & full

// Runtime variables:
static bool init_done = false;
static bool do_update_weather = false;
static bool bt_connected = true;
static bool night_mode = false;
static int moon_phase = 0;
static bool warning_last_update = false;
static int battery_percent = 70;
static GColor battery_color;
static int condition_y = 0; // y of the condition text layer, depends on the font which is chosen to fit the text
static int temp_dy = 0;     // vertical correction of the temperature text layer for the smaller fallback font
static GColor icon_color;
static GColor icon_bkgr_color;

// Screen obstruction (timeline peek): shift of all layers to the top in pixels
static bool will_be_obstructed = false;
static int obstruction_shift = 0;


static GColor get_weather_icon_color(int nr){
	switch (nr){
		case 34: return GColorIcterine;
		case 35: return GColorPictonBlue;
		case 36: return GColorFromHEX(0x55FFFF); //Rain
		case 37: return GColorChromeYellow;
		case 38: return GColorBlueMoon;
		case 39: return GColorFromHEX(0x55FFFF);
		case 40: return GColorChromeYellow;
		case 41: return GColorBlueMoon;
		case 42: return GColorPictonBlue;
		case 43: return GColorOrange;
		case 44: return GColorBlueMoon;
		case 45: return GColorCadetBlue;
		case 46: return GColorRajah;
		case 47: return GColorBlueMoon;
		case 49: return GColorPastelYellow;
		case 50: return GColorFromHEX(0x55AAAA);
		case 51: return GColorSunsetOrange; //hail
		case 57: return GColorCeleste; //snow
		case 58: return GColorYellow;
		case 59: return GColorCyan;
		case 60: return GColorLightGray; //fog
		case 61: return GColorPastelYellow;
		case 62: return GColorCadetBlue;
		case 63: return GColorLightGray; //haze
		case 64: return GColorChromeYellow;
		case 65: return GColorCadetBlue;
		case 66: return GColorCeleste; //wind
		case 70: return GColorRed;
		case 71: return GColorOrange;
		case 72: return GColorFromHEX(0x0055AA);
		case 73: return GColorYellow; //sun
		case 74: return GColorOrange;
		case 75: return GColorOrange;
		case 88: return GColorOrange; //tornado
		case 90: return GColorBabyBlueEyes; //temp_low
		case 93: return GColorRed; //temp_high
	}
	return GColorWhite;
}

static GColor get_weather_icon_bkgr_color(int nr){
	switch (nr){
		case 33: return GColorVividCerulean; //Cloud
		case 34: return GColorVividCerulean; //Cloud and Sun
		case 35: return GColorFromHEX(0x000055);
		case 36: return GColorFromHEX(0x555555); //Rain
		case 37: return GColorFromHEX(0x0055AA);
		case 38: return GColorFromHEX(0x000055);
		case 39: return GColorFromHEX(0x555555);
		case 40: return GColorFromHEX(0x0055AA);
		case 41: return GColorFromHEX(0x000055);
		case 42: return GColorFromHEX(0x555555);
		case 43: return GColorFromHEX(0x0055AA);
		case 44: return GColorFromHEX(0x000055);
		case 45: return GColorFromHEX(0x555555);
		case 46: return GColorFromHEX(0x0055AA);
		case 47: return GColorFromHEX(0x000055);
		case 48: return GColorElectricBlue;
		case 49: return GColorFromHEX(0x0055FF);
		case 50: return GColorFromHEX(0x000055);
		case 51: return GColorFromHEX(0x0055AA); //hail
		case 57: return GColorFromHEX(0x555555); //snow
		case 58: return GColorFromHEX(0x0055AA);
		case 59: return GColorFromHEX(0x000055);
		case 60: return GColorWhite; //fog
		case 61: return GColorFromHEX(0x0055AA);
		case 62: return GColorWhite;
		case 63: return GColorWhite; //haze
		case 64: return GColorWhite;
		case 65: return GColorWhite;
		case 66: return GColorFromHEX(0x0055AA); //wind
		case 73: return GColorFromHEX(0x0055FF); //sun
		case 74: return GColorFromHEX(0x0055AA);
		case 75: return GColorFromHEX(0x0055AA);
		case 88: return GColorFromHEX(0x555555); //tornado
		case 90: return GColorFromHEX(0x000055); //temp_low
		case 93: return GColorFromHEX(0xFFFF00); //temp_high
	}
	return GColorBlack;
}

static GColor get_temperature_color(int t){
	if (t >= 40) return GColorRed;
	if (t >= 28) return GColorOrange;
	if (t >= 23) return GColorChromeYellow;
	if (t >= 20) return GColorGreen;
	if (t >= 18) return GColorMalachite;
	if (t >= 15) return GColorIslamicGreen;
	if (t >= 10) return GColorJaegerGreen;
	if (t >= 6)  return GColorDarkGray;
	if (t >= 2)  return GColorElectricBlue;
	if (t >= -1) return GColorCyan;
	if (t >= -10) return GColorVividCerulean;
	if (t >= -20) return GColorPictonBlue;
	if (t >= -30) return GColorBlueMoon;
	return GColorCobaltBlue;
}

// the JS sends the degree sign as "__" (encoding problems on some phones)
static void replace_degree(char *s, int size_s){
	for (int i = 1; i < size_s; i++){
		if ((s[i-1] == '_') && (s[i] == '_')){
			s[i-1] = (char)194;
			s[i]   = (char)176;
		}
	}
}


#define MOVE_LAYER(layer, x, y, w, h) layer_set_frame(layer, GRect(x, (y)-obstruction_shift, w, h))
#define MOVE_TEXT_LAYER(layer, x, y, w, h) MOVE_LAYER(text_layer_get_layer(layer), x, y, w, h)

static void move_layers(void) {
	MOVE_LAYER(s_background_layer, 0, 0, 200, 228);
	MOVE_TEXT_LAYER(s_location_layer, 0, TEXT_Y(LOCATION_Y, LOCATION_H, GOTHIC_18_BOLD_TOP, GOTHIC_18_BOLD_H), 150, 24);
	MOVE_TEXT_LAYER(s_last_update_layer, 151, TEXT_Y(LOCATION_Y, LOCATION_H, GOTHIC_18_TOP, GOTHIC_18_H), 49, 24);
	MOVE_TEXT_LAYER(s_condition_layer, PADDING, condition_y, 200 - 2*PADDING, 60);
	MOVE_LAYER(s_icon_layer, 0, WEATHER_Y, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE);
	// temperature and min/max temperature as one block, vertically centered:
	MOVE_LAYER(s_rain_layer, WEATHER_ICON_SIZE, WEATHER_Y, RAIN_WIDTH, WEATHER_ICON_SIZE);
	int temp_y = WEATHER_Y + (WEATHER_ICON_SIZE - TEMP_H - PADDING - TEMP_MIN_MAX_H)/2;
	MOVE_TEXT_LAYER(s_temp_layer, WEATHER_ICON_SIZE + RAIN_WIDTH, temp_y - TEMP_TOP + temp_dy, 200 - WEATHER_ICON_SIZE - RAIN_WIDTH, 50);
	MOVE_TEXT_LAYER(s_temp_min_max_layer, WEATHER_ICON_SIZE + RAIN_WIDTH, temp_y + TEMP_H + PADDING - TEMP_MIN_MAX_TOP, 200 - WEATHER_ICON_SIZE - RAIN_WIDTH, 40);
	for (int i = 0; i < 4; i++) MOVE_LAYER(s_digit_layers[i], TIME_X + i*(DIGIT_WIDTH + DIGIT_GAP) + ((i >= 2) ? COLON_SIZE + DIGIT_GAP : 0), DIGIT_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
	// the weekdays (except Mittwoch) have a descender, the letters are centered with only half of it:
	MOVE_TEXT_LAYER(s_date_layer, 0, TEXT_Y(DATE_Y, DATE_H, GOTHIC_24_BOLD_TOP, GOTHIC_24_BOLD_H - GOTHIC_24_BOLD_DESC/2), 200, 30);
	MOVE_TEXT_LAYER(s_sleep_layer, 0, TEXT_Y(BOTTOM_Y, BOTTOM_H, SLEEP_TOP, SLEEP_H), BATTERY_X - 2, 30);
	MOVE_TEXT_LAYER(s_battery_text_layer, BATTERY_X, TEXT_Y(BATTERY_Y, BATTERY_HEIGHT, GOTHIC_18_BOLD_TOP, GOTHIC_18_BOLD_H), BATTERY_WIDTH, 24);
	MOVE_LAYER(effect_layer_get_layer(s_battery_fill_layer), BATTERY_X + BATTERY_BORDER, BATTERY_Y + BATTERY_BORDER, (BATTERY_WIDTH - 2*BATTERY_BORDER)*battery_percent/100, BATTERY_HEIGHT - 2*BATTERY_BORDER);
}

static void background_update_proc(Layer *layer, GContext* ctx){
	graphics_context_set_fill_color(ctx, COLOR_CLOCK_BKGR);
	graphics_fill_rect(ctx, GRect(0, 0, 200, 228), 0, GCornerNone);

	graphics_context_set_fill_color(ctx, (warning_location == 1) ? GColorRed : (warning_location == 2) ? GColorDarkGray : COLOR_HEADER_BKGR);
	graphics_fill_rect(ctx, GRect(0, LOCATION_Y, 150, LOCATION_H), 0, GCornerNone);
	graphics_context_set_fill_color(ctx, warning_last_update ? GColorRed : COLOR_HEADER_BKGR);
	graphics_fill_rect(ctx, GRect(151, LOCATION_Y, 49, LOCATION_H), 0, GCornerNone);
	graphics_context_set_fill_color(ctx, COLOR_CONDITION_BKGR);
	graphics_fill_rect(ctx, GRect(0, CONDITION_Y, 200, CONDITION_H), 0, GCornerNone);
	graphics_context_set_fill_color(ctx, bt_connected ? icon_bkgr_color : COLOR_BT_BKGR); // weather info in the color of the icon
	graphics_fill_rect(ctx, GRect(0, WEATHER_Y, 200, WEATHER_ICON_SIZE), 0, GCornerNone);
	graphics_context_set_fill_color(ctx, COLOR_DATE_BKGR);
	graphics_fill_rect(ctx, GRect(0, DATE_Y, 200, DATE_H), 0, GCornerNone);
	graphics_fill_rect(ctx, GRect(0, BOTTOM_Y, 200, BOTTOM_H), 0, GCornerNone); // sleep and battery, in the colors of the date

	graphics_context_set_stroke_color(ctx, COLOR_LINES);
	graphics_draw_line(ctx, GPoint(150, LOCATION_Y), GPoint(150, LOCATION_Y + LOCATION_H - 1));
	graphics_draw_line(ctx, GPoint(0, CONDITION_Y - 1), GPoint(199, CONDITION_Y - 1));
	graphics_draw_line(ctx, GPoint(0, WEATHER_Y - 1), GPoint(199, WEATHER_Y - 1));
	graphics_draw_line(ctx, GPoint(0, CLOCK_Y - 1), GPoint(199, CLOCK_Y - 1));
	graphics_draw_line(ctx, GPoint(0, BOTTOM_Y - 1), GPoint(199, BOTTOM_Y - 1));

	//colon of the time, at y 8 and 30 in the original 26x41 design:
	graphics_context_set_fill_color(ctx, COLOR_CLOCK);
	graphics_fill_rect(ctx, GRect(COLON_X, DIGIT_Y + 8*DIGIT_HEIGHT/SEVEN_SEGMENT_DESIGN_H, COLON_SIZE, 7*DIGIT_HEIGHT/SEVEN_SEGMENT_DESIGN_H), 0, GCornerNone);
	graphics_fill_rect(ctx, GRect(COLON_X, DIGIT_Y + 30*DIGIT_HEIGHT/SEVEN_SEGMENT_DESIGN_H, COLON_SIZE, 7*DIGIT_HEIGHT/SEVEN_SEGMENT_DESIGN_H), 0, GCornerNone);

	//battery (bottom right, gray body and nub, white inside), the charged part is inverted by s_battery_fill_layer:
	graphics_context_set_fill_color(ctx, COLOR_BATTERY_BORDER);
	graphics_fill_rect(ctx, GRect(BATTERY_X, BATTERY_Y, BATTERY_WIDTH, BATTERY_HEIGHT), 0, GCornerNone);
	graphics_fill_rect(ctx, GRect(BATTERY_X + BATTERY_WIDTH, BATTERY_Y + BATTERY_HEIGHT/2 - 3, 3, 7), 0, GCornerNone);
	graphics_context_set_fill_color(ctx, COLOR_BATTERY_BKGR);
	graphics_fill_rect(ctx, GRect(BATTERY_X + BATTERY_BORDER, BATTERY_Y + BATTERY_BORDER, BATTERY_WIDTH - 2*BATTERY_BORDER, BATTERY_HEIGHT - 2*BATTERY_BORDER), 0, GCornerNone);
}

static void digit_update_proc(Layer *layer, GContext* ctx) {
	graphics_context_set_fill_color(ctx, COLOR_CLOCK);
	seven_segment_paint(ctx, *(int *)layer_get_data(layer), layer_get_bounds(layer));
}

static void icon_update_proc(Layer *layer, GContext* ctx) {
	if (!bt_connected){
		graphics_context_set_fill_color(ctx, COLOR_BT_BKGR);
		graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
		bt_disconnected_draw(ctx, COLOR_BT, GColorRed);
		return;
	}
	graphics_context_set_fill_color(ctx, icon_bkgr_color);
	graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
	if (night_mode)
		moon_phase_draw(ctx, moon_phase, icon_color, COLOR_MOON_DARK);
	else
		weather_icon_draw(ctx, weather_icon, icon_color, icon_bkgr_color);

	// wind lines: 0..4 depending on the wind speed (and at least 3 for the plain wind icon)
	int lines = (wind_speed >= WIND_LINES_4) ? 4 : (wind_speed >= WIND_LINES_3) ? 3 : (wind_speed >= WIND_LINES_2) ? 2 : (wind_speed >= WIND_LINES_1) ? 1 : 0;
	if (!night_mode && (weather_icon == 'B') && (lines < 3)) lines = 3;
	wind_lines_draw(ctx, lines, COLOR_WIND, COLOR_WIND_OUTLINE);
}

// rain drop (tip at the top, height 3*r)
static void draw_drop(GContext *ctx, GPoint tip, int r, GColor color) {
	GPoint c = GPoint(tip.x, tip.y + 2*r);
	GPoint pts[3] = {tip, GPoint(c.x - r*9/10, c.y - r*4/10), GPoint(c.x + r*9/10, c.y - r*4/10)};
	GPath path = {.num_points = 3, .points = pts, .rotation = 0, .offset = GPointZero};
	graphics_context_set_fill_color(ctx, color);
	gpath_draw_filled(ctx, &path);
	graphics_fill_circle(ctx, c, r);
}

// next rain: a drop and "3h" (today), "1d" (tomorrow) .. "5d", only the drop if it rains now, a striked through drop if no rain is in sight
static void rain_update_proc(Layer *layer, GContext* ctx) {
	static char text[16];
	snprintf(text, sizeof(text), (rain_in >= 100) ? "%dd" : "%dh", (rain_in >= 100) ? rain_in - 100 : rain_in);
	bool with_text = (rain_in > 0);
	const int r = 9, cx = RAIN_WIDTH/2;
	int y = (WEATHER_ICON_SIZE - 3*r - (with_text ? PADDING + GOTHIC_28_BOLD_H : 0))/2; // drop and text as one block, vertically centered

	graphics_context_set_antialiased(ctx, true);
	draw_drop(ctx, GPoint(cx, y), r, icon_color);
	if (rain_in < 0){
		draw_drop(ctx, GPoint(cx, y + 4), r - 2, bt_connected ? icon_bkgr_color : COLOR_BT_BKGR);
		graphics_context_set_stroke_color(ctx, icon_color);
		graphics_context_set_stroke_width(ctx, 2);
		graphics_draw_line(ctx, GPoint(cx - r - 2, y + 1), GPoint(cx + r + 2, y + 3*r - 1));
	}
	if (with_text){
		graphics_context_set_text_color(ctx, icon_color);
		graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(0, y + 3*r + PADDING - GOTHIC_28_BOLD_TOP, RAIN_WIDTH, 30), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
	}
}


static void load_data(void) {
	if (persist_exists(KEY_LOCATION_NAME)) persist_read_string(KEY_LOCATION_NAME, location_name, sizeof(location_name));
	if (persist_exists(KEY_LOCATION_LAT)) location_latitude = persist_read_int(KEY_LOCATION_LAT);
	if (persist_exists(KEY_WEATHER_TEMP)) weather_temp = persist_read_int(KEY_WEATHER_TEMP);
	if (persist_exists(KEY_WEATHER_ICON)) weather_icon = persist_read_int(KEY_WEATHER_ICON);
	if (persist_exists(KEY_WEATHER_WIND)) wind_speed = persist_read_int(KEY_WEATHER_WIND);
	if (persist_exists(KEY_WEATHER_RAIN)) rain_in = persist_read_int(KEY_WEATHER_RAIN);
	if (persist_exists(KEY_WEATHER_STRING_1)) persist_read_string(KEY_WEATHER_STRING_1, temp_min_max, sizeof(temp_min_max));
	if (persist_exists(KEY_WEATHER_STRING_2)) persist_read_string(KEY_WEATHER_STRING_2, weather_condition, sizeof(weather_condition));
	if (persist_exists(KEY_TIME_LAST_UPDATE)) phone_last_updated = (time_t)persist_read_int(KEY_TIME_LAST_UPDATE);
	if (persist_exists(KEY_SUN_RISE_UNIX)) sun_rise_unix_loc = (time_t)persist_read_int(KEY_SUN_RISE_UNIX);
	if (persist_exists(KEY_SUN_SET_UNIX)) sun_set_unix_loc = (time_t)persist_read_int(KEY_SUN_SET_UNIX);
	if (persist_exists(KEY_WARN_LOCATION)) warning_location = persist_read_int(KEY_WARN_LOCATION);
	if (persist_exists(KEY_BTY_LAST_STATE)) last_charge_state = persist_read_int(KEY_BTY_LAST_STATE);
}

static void save_data(void) {
	persist_write_string(KEY_LOCATION_NAME, location_name);
	persist_write_int(KEY_LOCATION_LAT, location_latitude);
	persist_write_int(KEY_WEATHER_TEMP, weather_temp);
	persist_write_int(KEY_WEATHER_ICON, weather_icon);
	persist_write_int(KEY_WEATHER_WIND, wind_speed);
	persist_write_int(KEY_WEATHER_RAIN, rain_in);
	persist_write_string(KEY_WEATHER_STRING_1, temp_min_max);
	persist_write_string(KEY_WEATHER_STRING_2, weather_condition);
	persist_write_int(KEY_TIME_LAST_UPDATE, (int)phone_last_updated);
	persist_write_int(KEY_SUN_RISE_UNIX, (int)sun_rise_unix_loc);
	persist_write_int(KEY_SUN_SET_UNIX, (int)sun_set_unix_loc);
	persist_write_int(KEY_WARN_LOCATION, warning_location);
	persist_write_int(KEY_BTY_LAST_STATE, last_charge_state);
}

// time since the last weather update, red if older than the update interval
static void display_last_updated(void) {
	static char buffer[16];
	time_t age = time(NULL) - phone_last_updated;
	bool warning = true;
	if (phone_last_updated == 0){
		snprintf(buffer, sizeof(buffer), "--:--");
	} else {
		if (age < 60) snprintf(buffer, sizeof(buffer), "%d s", (int)age);
		else if (age < 3600) snprintf(buffer, sizeof(buffer), "%d m", (int)(age/60));
		else if (age < 24*3600) snprintf(buffer, sizeof(buffer), "%d h", (int)(age/3600));
		else snprintf(buffer, sizeof(buffer), "%d d", (int)(age/(24*3600)));
		warning = (age > WEATHER_UPDATE_INTERVAL_MINUTES*60);
	}
	text_layer_set_text(s_last_update_layer, buffer);
	if (warning != warning_last_update){
		warning_last_update = warning;
		layer_mark_dirty(s_background_layer);
	}
}

static void display_weather(void) {
	static char temp_buffer[12];
	snprintf(temp_buffer, sizeof(temp_buffer), "%d°C", weather_temp);
	text_layer_set_text(s_temp_layer, temp_buffer);
	// e.g. "-12°C" does not fit beside the rain indicator in size 36, then the condition font of size 30 is used
	bool fits = graphics_text_layout_get_content_size(temp_buffer, s_temp_font, GRect(0, 0, 500, 60), GTextOverflowModeFill, GTextAlignmentLeft).w <= 200 - WEATHER_ICON_SIZE - RAIN_WIDTH;
	text_layer_set_font(s_temp_layer, fits ? s_temp_font : s_condition_fonts[1]);
	temp_dy = fits ? 0 : TEMP_TOP - CONDITION_30_TOP + 2;
	text_layer_set_text(s_temp_min_max_layer, temp_min_max);

	// weather condition: the biggest font in which it fits on one line, else two lines of the smallest one
	static const int tops[4] = {CONDITION_36_TOP, CONDITION_30_TOP, CONDITION_24_TOP, CONDITION_18_TOP};
	static const int heights[4] = {CONDITION_36_H, CONDITION_30_H, CONDITION_24_H, CONDITION_18_H};
	static const int sizes[4] = {36, 30, 24, 18};
	int f = 0, h = 0;
	bool descenders = (strpbrk(weather_condition, "gjpqy") != NULL);
	for (f = 0; f < 4; f++){
		h = graphics_text_layout_get_content_size(weather_condition, s_condition_fonts[f], GRect(0, 0, 200 - 2*PADDING, 100), GTextOverflowModeWordWrap, GTextAlignmentCenter).h;
		bool fits_height = (heights[f] - (descenders ? 0 : sizes[f]/4) <= CONDITION_H - 2*PADDING);
		if (((h < sizes[f]*3/2) && fits_height) || (f == 3)) break; // a single line
	}
	text_layer_set_font(s_condition_layer, s_condition_fonts[f]);
	text_layer_set_text(s_condition_layer, weather_condition);
	int block = heights[f] + ((h < sizes[f]*3/2) ? 0 : CONDITION_18_PITCH);
	if (!descenders) block -= sizes[f]/4; // center the letters only
	condition_y = TEXT_Y(CONDITION_Y, CONDITION_H, tops[f], block);
	move_layers();
	text_layer_set_text(s_location_layer, location_name);
	display_last_updated();
}

// at night (between sun set and sun rise) the moon phase replaces the weather icon
static void update_icon(void) {
	time_t now = time(NULL);
	struct tm now_tm = *localtime(&now);
	struct tm rise = *localtime(&sun_rise_unix_loc);
	struct tm set  = *localtime(&sun_set_unix_loc);
	int t = now_tm.tm_hour*60 + now_tm.tm_min;
	night_mode = (t < rise.tm_hour*60 + rise.tm_min) || (t >= set.tm_hour*60 + set.tm_min);
	moon_phase = calc_moonphase_number(location_latitude/1E6);
	icon_color = night_mode ? GColorWhite : get_weather_icon_color(weather_icon);
	icon_bkgr_color = night_mode ? GColorBlack : get_weather_icon_bkgr_color(weather_icon);
	text_layer_set_text_color(s_temp_layer, get_temperature_color(weather_temp));
	text_layer_set_text_color(s_temp_min_max_layer, icon_color);
	layer_mark_dirty(s_icon_layer);
	layer_mark_dirty(s_rain_layer);
	layer_mark_dirty(s_background_layer);
}

static void request_weather(void) {
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	dict_write_uint8(iter, 0, 0);
	app_message_outbox_send();
}

static void handle_tick(struct tm* tick_time, TimeUnits units_changed) {
	int hour = tick_time->tm_hour;
	if (!clock_is_24h_style()){
		hour = hour % 12;
		if (hour == 0) hour = 12;
	}
	int digits[4] = {(hour/10) ? hour/10 : -1, hour%10, tick_time->tm_min/10, tick_time->tm_min%10};
	for (int i = 0; i < 4; i++){
		*(int *)layer_get_data(s_digit_layers[i]) = digits[i];
		layer_mark_dirty(s_digit_layers[i]);
	}

	static int vibe_hour_old = -1;
	if (VIBE_ON_HOUR && (vibe_hour_old >= 0) && (vibe_hour_old != tick_time->tm_hour) && !quiet_time_is_active()){
		static const uint32_t segments[] = { 150, 70, 150 };
		vibes_enqueue_custom_pattern((VibePattern){.durations = segments, .num_segments = ARRAY_LENGTH(segments)});
	}
	vibe_hour_old = tick_time->tm_hour;

	static char date_buffer[24];
	snprintf(date_buffer, sizeof(date_buffer), "%s %02d.%02d.", s_weekdays[tick_time->tm_wday], tick_time->tm_mday, tick_time->tm_mon+1);
	text_layer_set_text(s_date_layer, date_buffer);

	update_icon();
	display_last_updated();

	if (init_done && (do_update_weather || (time(NULL) - phone_last_updated >= WEATHER_UPDATE_INTERVAL_MINUTES*60-60))){
		do_update_weather = false;
		request_weather();
	}
}

static void handle_battery(BatteryChargeState charge_state) {
	int old_charge_state = last_charge_state;
	last_charge_state = charge_state.is_plugged ? (charge_state.is_charging ? 1 : 2) : 0;
	// backlight on while plugged in and full
	if (LIGHT_WHEN_PLUGGED && (old_charge_state != last_charge_state)) light_enable(last_charge_state == 2);

	battery_percent = charge_state.charge_percent;
	static char battery_buffer[8];
	snprintf(battery_buffer, sizeof(battery_buffer), (last_charge_state == 1) ? "*%d%%" : "%d%%", battery_percent);
	text_layer_set_text(s_battery_text_layer, battery_buffer);

	// green, orange at <= 20 %, red at <= 10 %; the text is drawn in that color, the fill layer swaps it with the white background
	battery_color = (battery_percent > 20) ? GColorIslamicGreen : (battery_percent > 10) ? GColorOrange : GColorRed;
	text_layer_set_text_color(s_battery_text_layer, battery_color);
	GlobalInverterColor = battery_color.argb & 0b00111111;
	GlobalBkgColor      = COLOR_BATTERY_BKGR.argb & 0b00111111;
	move_layers();
	layer_mark_dirty(s_background_layer);
}

static void handle_bluetooth(bool connected) {
	if (init_done && (connected != bt_connected) && VIBE_ON_DISCONNECT){
		static const uint32_t disconnect[] = { 200, 70, 200, 70, 200 };
		static const uint32_t reconnect[] = { 70, 70, 150, 150, 70, 70, 150, 150 };
		if (connected) vibes_enqueue_custom_pattern((VibePattern){.durations = reconnect, .num_segments = ARRAY_LENGTH(reconnect)});
		else vibes_enqueue_custom_pattern((VibePattern){.durations = disconnect, .num_segments = ARRAY_LENGTH(disconnect)});
	}
	if (connected && !bt_connected && init_done) do_update_weather = true;
	bt_connected = connected;
	layer_mark_dirty(s_icon_layer);
	layer_mark_dirty(s_background_layer);
}

static void health_handler(HealthEventType event, void *context) {
	if ((event != HealthEventSignificantUpdate) && (event != HealthEventSleepUpdate)) return;
	// sleep of today and in brackets the deep sleep part of it:
	static char sleep_str[32];
	HealthValue sleep = health_service_sum_today(HealthMetricSleepSeconds);
	HealthValue deep  = health_service_sum_today(HealthMetricSleepRestfulSeconds);
	snprintf(sleep_str, sizeof(sleep_str), "%dh%02d (%dh%02d)", (int)(sleep/3600), (int)((sleep%3600)/60), (int)(deep/3600), (int)((deep%3600)/60));
	text_layer_set_text(s_sleep_layer, sleep_str);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
	for (Tuple *t = dict_read_first(iterator); t != NULL; t = dict_read_next(iterator)) {
		switch (t->key) {
			case KEY_LOCATION_NAME:
				snprintf(location_name, sizeof(location_name), "%s", t->value->cstring);
				phone_last_updated = time(NULL);
				break;
			case KEY_LOCATION_LAT:     location_latitude = (int)t->value->int32; break;
			case KEY_WEATHER_TEMP:     weather_temp = (int)t->value->int32; break;
			case KEY_WEATHER_ICON:     weather_icon = (int)t->value->int32; break;
			case KEY_WEATHER_WIND:     wind_speed = (int)t->value->int32; break;
			case KEY_WEATHER_RAIN:     rain_in = (int)t->value->int32; break;
			case KEY_SUN_RISE_UNIX:    sun_rise_unix_loc = (time_t)t->value->int32; break;
			case KEY_SUN_SET_UNIX:     sun_set_unix_loc = (time_t)t->value->int32; break;
			case KEY_WARN_LOCATION:
				warning_location = (int)t->value->int32;
				layer_mark_dirty(s_background_layer);
				break;
			case KEY_WEATHER_STRING_1:
				snprintf(temp_min_max, sizeof(temp_min_max), "%s", t->value->cstring);
				replace_degree(temp_min_max, sizeof(temp_min_max));
				break;
			case KEY_WEATHER_STRING_2:
				snprintf(weather_condition, sizeof(weather_condition), "%s", t->value->cstring);
				break;
		}
	}
	save_data();
	display_weather();
	update_icon();
}

static void unobstructed_area_will_change(GRect final_unobstructed_area, void *context) {
	GRect full_bounds = layer_get_bounds(s_window_layer);
	will_be_obstructed = grect_equal(&full_bounds, &final_unobstructed_area);
}

static void unobstructed_area_change(AnimationProgress progress, void *context) {
	obstruction_shift = 18 * progress / (ANIMATION_NORMALIZED_MAX - ANIMATION_NORMALIZED_MIN);
	if (will_be_obstructed) obstruction_shift = 18 - obstruction_shift;
	move_layers();
}

static TextLayer *create_text_layer(GFont font, GColor color) {
	TextLayer *layer = text_layer_create(GRectZero);
	text_layer_set_background_color(layer, GColorClear);
	text_layer_set_text_color(layer, color);
	text_layer_set_text_alignment(layer, GTextAlignmentCenter);
	text_layer_set_font(layer, font);
	layer_add_child(s_window_layer, text_layer_get_layer(layer));
	return layer;
}

static void main_window_load(Window *window) {
	s_window_layer = window_get_root_layer(window);
	GRect real_bounds = layer_get_unobstructed_bounds(s_window_layer);
	GRect bounds = layer_get_bounds(s_window_layer);
	obstruction_shift = (bounds.size.h == real_bounds.size.h) ? 0 : 18;

	load_data();

	s_background_layer = layer_create(GRectZero);
	layer_set_update_proc(s_background_layer, background_update_proc);
	layer_add_child(s_window_layer, s_background_layer);

	for (int i = 0; i < 4; i++){
		s_digit_layers[i] = layer_create_with_data(GRectZero, sizeof(int));
		*(int *)layer_get_data(s_digit_layers[i]) = -1;
		layer_set_update_proc(s_digit_layers[i], digit_update_proc);
		layer_add_child(s_window_layer, s_digit_layers[i]);
	}

	s_icon_layer = layer_create(GRectZero);
	layer_set_update_proc(s_icon_layer, icon_update_proc);
	layer_add_child(s_window_layer, s_icon_layer);

	s_rain_layer = layer_create(GRectZero);
	layer_set_update_proc(s_rain_layer, rain_update_proc);
	layer_add_child(s_window_layer, s_rain_layer);

	s_condition_fonts[0] = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CONDITION_36));
	s_condition_fonts[1] = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CONDITION_30));
	s_condition_fonts[2] = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CONDITION_24));
	s_condition_fonts[3] = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CONDITION_18));
	s_temp_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEMP_36));
	s_temp_min_max_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEMP_MIN_MAX_22));

	s_location_layer     = create_text_layer(fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), COLOR_HEADER_TEXT);
	s_last_update_layer  = create_text_layer(fonts_get_system_font(FONT_KEY_GOTHIC_18), COLOR_HEADER_TEXT);
	s_condition_layer    = create_text_layer(s_condition_fonts[0], COLOR_CONDITION_TEXT);
	s_temp_layer         = create_text_layer(s_temp_font, GColorWhite);
	s_temp_min_max_layer = create_text_layer(s_temp_min_max_font, GColorWhite);
	s_date_layer         = create_text_layer(fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), COLOR_DATE_TEXT);
	s_sleep_layer        = create_text_layer(fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), COLOR_DATE_TEXT);
	s_battery_text_layer = create_text_layer(fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorIslamicGreen);

	// after the battery text, so that the effect inverts it too:
	s_battery_fill_layer = effect_layer_create(GRectZero);
	effect_layer_add_effect(s_battery_fill_layer, effect_invert_color, (void *)0b00000000); //use global inverter color
	layer_add_child(s_window_layer, effect_layer_get_layer(s_battery_fill_layer));

	move_layers();
	display_weather();

	time_t now = time(NULL);
	handle_tick(localtime(&now), MINUTE_UNIT | HOUR_UNIT | DAY_UNIT);
	handle_battery(battery_state_service_peek());
	handle_bluetooth(connection_service_peek_pebble_app_connection());

	tick_timer_service_subscribe(MINUTE_UNIT, handle_tick);
	battery_state_service_subscribe(handle_battery);
	connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = handle_bluetooth});
	if (!health_service_events_subscribe(health_handler, NULL)) APP_LOG(APP_LOG_LEVEL_ERROR, "Health not available!");
	unobstructed_area_service_subscribe((UnobstructedAreaHandlers){.will_change = unobstructed_area_will_change, .change = unobstructed_area_change}, NULL);

	app_message_register_inbox_received(inbox_received_callback);
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

	init_done = true;
	do_update_weather = true; // fresh weather on every start
}

static void main_window_unload(Window *window) {
	save_data();
	tick_timer_service_unsubscribe();
	battery_state_service_unsubscribe();
	connection_service_unsubscribe();
	health_service_events_unsubscribe();
	unobstructed_area_service_unsubscribe();

	layer_destroy(s_background_layer);
	for (int i = 0; i < 4; i++) layer_destroy(s_digit_layers[i]);
	layer_destroy(s_icon_layer);
	layer_destroy(s_rain_layer);
	effect_layer_destroy(s_battery_fill_layer);
	text_layer_destroy(s_location_layer);
	text_layer_destroy(s_last_update_layer);
	text_layer_destroy(s_condition_layer);
	text_layer_destroy(s_temp_layer);
	text_layer_destroy(s_temp_min_max_layer);
	text_layer_destroy(s_date_layer);
	text_layer_destroy(s_sleep_layer);
	text_layer_destroy(s_battery_text_layer);
	for (int i = 0; i < 4; i++) fonts_unload_custom_font(s_condition_fonts[i]);
	fonts_unload_custom_font(s_temp_font);
	fonts_unload_custom_font(s_temp_min_max_font);
}

int main(void) {
	s_main_window = window_create();
	window_set_window_handlers(s_main_window, (WindowHandlers) {
		.load = main_window_load,
		.unload = main_window_unload,
	});
	window_stack_push(s_main_window, true);
	app_event_loop();
	window_destroy(s_main_window);
}
