#include <pebble.h>
#include "config.h"
#include "keys.h"
#include "mooncalc.h"
#include "seven_segment.h"
#include "weather_icons.h"
#include "effect_layer.h"

//deploy:
/*
pebble build
pebble install --phone <ip address>
*/
// Pebble Time 2 (emery, 200x228)
// Rows from top to bottom: location | last update, weather condition, weather icon | next rain | temperature + min/max temperature, time, date, sleep (deep sleep) | battery
// At the times of the trains (config.h) the date and the sleep are replaced by the ÖBB departures.
// Each row is as high as its content plus PADDING above and below (the clock, the date and the bottom row 2px more). The weather condition takes the remaining space, which is the height of the date row.
// Rows with the same background are separated by a 1px line.
#define PADDING 2
#define BIG_PADDING  (PADDING + 2) // the date and the bottom row
#define TIME_PADDING (PADDING + 4) // the time

#define DIGIT_WIDTH     40
#define DIGIT_HEIGHT    66
#define COLON_SIZE      (7*DIGIT_WIDTH/SEVEN_SEGMENT_DESIGN_W) // the colon squares are 7 pixels in the original 26x41 design
#define DIGIT_GAP       6  // between the digits and to the colon
#define TIME_X          ((200 - 4*DIGIT_WIDTH - 4*DIGIT_GAP - COLON_SIZE)/2)
#define COLON_X         (TIME_X + 2*(DIGIT_WIDTH + DIGIT_GAP))
#define RAIN_WIDTH      48 // next rain indicator between weather icon and temperature
#define TRAIN_GAP       6  // between the departures
#define MAX_DEPARTURES  10
#define NOTIF_ICON_SIZE 27 // notification icons of the phone in the bottom row instead of the sleep, as high as the row (ICON_SIZE in the Android app)
#define NOTIF_ICON_BYTES ((NOTIF_ICON_SIZE*NOTIF_ICON_SIZE + 7)/8)
#define NOTIF_ICON_GAP  4
#define MAX_NOTIF_ICONS 6

// All texts except the time and the departures are in Montserrat Medium (the last update in Montserrat Regular, the date and the sleep in Montserrat SemiBold), in the sizes of the names. Measured glyph extents (like the
// Pebble font generator renders them): top offset of the capitals and digits within the text layer, their height down to the baseline, and the descenders.
// The departures are in the system font Gothic Bold, GOTHIC_* measured in the emulator.
#define TEXT_14_TOP          3
#define TEXT_14_H            10
#define TEXT_16_TOP          3
#define TEXT_16_H            12
#define TEXT_16R_TOP         3    // Regular
#define TEXT_16R_H           12
#define TEXT_18_TOP          4
#define TEXT_18_H            13   // SemiBold the same
#define TEXT_18_PAREN_H      17   // incl. the parentheses below the baseline
#define TEXT_22_TOP          5
#define TEXT_22_H            16   // SemiBold the same
#define TEXT_22_DESC         4
#define TEXT_24_TOP          6
#define TEXT_24_H            17
#define TEXT_32_TOP          9
#define TEXT_32_H            22
#define TEXT_36_TOP          10
#define TEXT_36_H            25
#define GOTHIC_24_TOP        10
#define GOTHIC_24_H          14
#define GOTHIC_28_TOP        10
#define GOTHIC_28_H          18

#define LOCATION_Y   0
#define LOCATION_H   (TEXT_16_H + 2*PADDING)
#define CONDITION_Y  (LOCATION_Y + LOCATION_H + 1)
#define CONDITION_H  (WEATHER_Y - 1 - CONDITION_Y)
#define WEATHER_Y    (CLOCK_Y - 1 - WEATHER_ICON_SIZE)
#define CLOCK_Y      (DATE_Y - CLOCK_H)
#define CLOCK_H      (DIGIT_HEIGHT + 2*TIME_PADDING)
#define DATE_Y       (BOTTOM_Y - 1 - DATE_H)
#define DATE_H       (TEXT_22_H + TEXT_22_DESC/2 + 2*BIG_PADDING)
#define BOTTOM_Y     (228 - BOTTOM_H)
#define BOTTOM_H     (BATTERY_HEIGHT + 2*BIG_PADDING) // the sleep text (TEXT_18_PAREN_H) is a bit lower than the battery
#define DIGIT_Y      (CLOCK_Y + TIME_PADDING)
#define BATTERY_Y    (BOTTOM_Y + BIG_PADDING)
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
#define COLOR_WEATHER_TEXT    GColorWhite      // next rain, min/max temperature (the temperature in the color of get_temperature_color); only the weather icon has the background color of the weather
#define COLOR_WEATHER_BKGR    GColorBlack
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
#define COLOR_WIND            GColorPictonBlue
#define COLOR_WIND_OUTLINE    GColorWhite

static Window *s_main_window;
static Layer *s_window_layer;
static Layer *s_background_layer;
static Layer *s_digit_layers[4]; // each with an int as data: the digit to paint, -1 = none
static Layer *s_icon_layer;      // weather icon / moon phase / bluetooth disconnected, plus wind lines
static Layer *s_rain_layer;      // next rain indicator
static Layer *s_trains_layer;    // departures over the date and the sleep
static Layer *s_notif_layer;     // notification icons of the phone instead of the sleep
static TextLayer *s_temp_layer;
static TextLayer *s_location_layer;
static TextLayer *s_last_update_layer;
static TextLayer *s_condition_layer;
static GFont s_font_14, s_font_16, s_font_16r, s_font_18, s_font_18sb, s_font_22, s_font_22sb, s_font_24, s_font_32, s_font_36; // see TEXT_*, sb = SemiBold
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
static char departures[6*MAX_DEPARTURES + 1]; // see KEY_TRAIN_DEPARTURES
static uint8_t notif_icons[1 + MAX_NOTIF_ICONS*NOTIF_ICON_BYTES]; // see KEY_NOTIF_ICONS, not persisted (sent again when the watchface is opened)
static time_t train_last_updated = 0;
static int train_done = -1; // tm_yday*10 + 1 / 2: the morning / evening of that day is over

// Runtime variables:
static bool init_done = false;
static bool do_update_weather = false;
static bool bt_connected = true;
static bool night_mode = false;
static int moon_phase = 0;
static bool warning_last_update = false;
static bool weather_outdated = false; // see WEATHER_HIDE_AFTER_MINUTES
static int battery_percent = 70;
static GColor battery_color;
static int condition_y = 0; // y and height of the condition text layer, depend on the font which is chosen to fit the text
static int condition_h = 30;
static int temp_dy = 0;     // vertical correction of the temperature layer for the smaller fallback font
static char temp_text[12];
static GColor icon_color;
static GColor icon_bkgr_color;
static int train_mode = 0; // 0: off, 1: morning (Pressbaum -> Westbahnhof), 2: evening (Westbahnhof -> Pressbaum)
static bool trains_shown = false; // the departures cover the date and the bottom row
static time_t train_last_request = 0;

// Screen obstruction (timeline peek): shift of all layers to the top in pixels
static bool will_be_obstructed = false;
static int obstruction_shift = 0;


static GColor get_weather_icon_color(int nr){
	switch (nr){
		case 34: return GColorIcterine;
		case 36: return GColorFromHEX(0x55FFFF); //Rain
		case 39: return GColorFromHEX(0x55FFFF);
		case 42: return GColorPictonBlue;
		case 45: return GColorCadetBlue;
		case 57: return GColorCeleste; //snow
		case 60: return GColorLightGray; //fog
		case 63: return GColorLightGray; //haze
		case 66: return GColorCeleste; //wind
		case 70: return GColorRed;
		case 73: return GColorYellow; //sun
		case 88: return GColorOrange; //tornado
	}
	return GColorWhite;
}

static GColor get_weather_icon_bkgr_color(int nr){
	switch (nr){
		case 33: return GColorVividCerulean; //Cloud
		case 34: return GColorVividCerulean; //Cloud and Sun
		case 36: return GColorFromHEX(0x555555); //Rain
		case 39: return GColorFromHEX(0x555555);
		case 42: return GColorFromHEX(0x555555);
		case 45: return GColorFromHEX(0x555555);
		case 48: return GColorElectricBlue;
		case 57: return GColorFromHEX(0x555555); //snow
		case 60: return GColorWhite; //fog
		case 63: return GColorWhite; //haze
		case 66: return GColorFromHEX(0x0055AA); //wind
		case 73: return GColorFromHEX(0x0055FF); //sun
		case 88: return GColorFromHEX(0x555555); //tornado
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
	MOVE_TEXT_LAYER(s_location_layer, 0, TEXT_Y(LOCATION_Y, LOCATION_H, TEXT_16_TOP, TEXT_16_H), 150, 24);
	MOVE_TEXT_LAYER(s_last_update_layer, 151, TEXT_Y(LOCATION_Y, LOCATION_H, TEXT_16R_TOP, TEXT_16R_H), 49, 24);
	MOVE_TEXT_LAYER(s_condition_layer, PADDING, condition_y, 200 - 2*PADDING, condition_h);
	MOVE_LAYER(s_icon_layer, 0, WEATHER_Y, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE);
	// temperature and min/max temperature as one block, vertically centered:
	MOVE_LAYER(s_rain_layer, WEATHER_ICON_SIZE, WEATHER_Y, RAIN_WIDTH, WEATHER_ICON_SIZE);
	int temp_y = WEATHER_Y + (WEATHER_ICON_SIZE - TEXT_36_H - 2*PADDING - TEXT_22_H)/2;
	MOVE_TEXT_LAYER(s_temp_layer, WEATHER_ICON_SIZE + RAIN_WIDTH, temp_y - TEXT_36_TOP + temp_dy, 200 - WEATHER_ICON_SIZE - RAIN_WIDTH, 50);
	MOVE_TEXT_LAYER(s_temp_min_max_layer, WEATHER_ICON_SIZE + RAIN_WIDTH, temp_y + TEXT_36_H + 2*PADDING - TEXT_22_TOP, 200 - WEATHER_ICON_SIZE - RAIN_WIDTH, 40);
	for (int i = 0; i < 4; i++) MOVE_LAYER(s_digit_layers[i], TIME_X + i*(DIGIT_WIDTH + DIGIT_GAP) + ((i >= 2) ? COLON_SIZE + DIGIT_GAP : 0), DIGIT_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
	// the weekdays (except Mittwoch) have a descender, the letters are centered with only half of it:
	MOVE_TEXT_LAYER(s_date_layer, 0, TEXT_Y(DATE_Y, DATE_H, TEXT_22_TOP, TEXT_22_H + TEXT_22_DESC/2), 200, 30);
	MOVE_TEXT_LAYER(s_sleep_layer, 0, TEXT_Y(BOTTOM_Y, BOTTOM_H, TEXT_18_TOP, TEXT_18_PAREN_H), BATTERY_X - 2, 30);
	MOVE_LAYER(s_notif_layer, 0, BOTTOM_Y + (BOTTOM_H - NOTIF_ICON_SIZE)/2, BATTERY_X - 2, NOTIF_ICON_SIZE);
	MOVE_LAYER(s_trains_layer, 0, DATE_Y, 200, BOTTOM_Y + BOTTOM_H - DATE_Y);
	MOVE_TEXT_LAYER(s_battery_text_layer, BATTERY_X, TEXT_Y(BATTERY_Y, BATTERY_HEIGHT, TEXT_16_TOP, TEXT_16_H), BATTERY_WIDTH, 24);
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
	graphics_context_set_fill_color(ctx, COLOR_WEATHER_BKGR);
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
	graphics_context_set_fill_color(ctx, weather_outdated ? COLOR_BT_BKGR : icon_bkgr_color);
	graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
	if (night_mode)
		moon_phase_draw(ctx, moon_phase, icon_color, COLOR_MOON_DARK); // calculated on the watch, also without the weather
	if (weather_outdated) return;
	if (!night_mode) weather_icon_draw(ctx, weather_icon, icon_color, icon_bkgr_color);

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

// next rain: a drop and "3h" (today), "1d" (tomorrow), the weekday ("Mo") if later, only the drop if it rains now, a striked through drop if no rain is in sight
static void rain_update_proc(Layer *layer, GContext* ctx) {
	static const char *weekdays[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
	static char text[16];
	time_t now = time(NULL);
	if (rain_in >= 102) snprintf(text, sizeof(text), "%s", weekdays[(localtime(&now)->tm_wday + rain_in - 100) % 7]);
	else snprintf(text, sizeof(text), (rain_in >= 100) ? "%dd" : "%dh", (rain_in >= 100) ? rain_in - 100 : rain_in);
	bool with_text = (rain_in > 0);
	const int r = 9, cx = RAIN_WIDTH/2;
	int y = (WEATHER_ICON_SIZE - 3*r - (with_text ? PADDING + TEXT_24_H : 0))/2; // drop and text as one block, vertically centered

	graphics_context_set_antialiased(ctx, true);
	draw_drop(ctx, GPoint(cx, y), r, COLOR_WEATHER_TEXT);
	if (rain_in < 0){
		draw_drop(ctx, GPoint(cx, y + 4), r - 2, COLOR_WEATHER_BKGR);
		graphics_context_set_stroke_color(ctx, COLOR_WEATHER_TEXT);
		graphics_context_set_stroke_width(ctx, 2);
		graphics_draw_line(ctx, GPoint(cx - r - 2, y + 1), GPoint(cx + r + 2, y + 3*r - 1));
	}
	if (with_text){
		graphics_context_set_text_color(ctx, COLOR_WEATHER_TEXT);
		graphics_draw_text(ctx, text, s_font_24, GRect(0, y + 3*r + PADDING - TEXT_24_TOP, RAIN_WIDTH, 30), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
	}
}

// the notification icons of the phone in the color of the date, as many as fit left of the battery, centered
static void notif_update_proc(Layer *layer, GContext* ctx) {
	int n = notif_icons[0], fit = (BATTERY_X - 2 + NOTIF_ICON_GAP)/(NOTIF_ICON_SIZE + NOTIF_ICON_GAP);
	if (n > fit) n = fit;
	int x0 = (BATTERY_X - 2 - n*(NOTIF_ICON_SIZE + NOTIF_ICON_GAP) + NOTIF_ICON_GAP)/2;
	graphics_context_set_stroke_color(ctx, COLOR_DATE_TEXT);
	for (int i = 0; i < n; i++){
		const uint8_t *bits = notif_icons + 1 + i*NOTIF_ICON_BYTES;
		for (int p = 0; p < NOTIF_ICON_SIZE*NOTIF_ICON_SIZE; p++) if (bits[p/8] & (0x80 >> (p%8))) graphics_draw_pixel(ctx, GPoint(x0 + i*(NOTIF_ICON_SIZE + NOTIF_ICON_GAP) + p%NOTIF_ICON_SIZE, p/NOTIF_ICON_SIZE));
	}
}

// the next departures, e.g. "6:42s" (S-Bahn) or "6:51" (REX, bigger), centered in the date row and the bottom row (over the battery); delayed ones in red with the expected time, cancelled ones struck through
static void trains_update_proc(Layer *layer, GContext* ctx) {
	time_t now = time(NULL);
	struct tm *now_tm = localtime(&now);
	int t = now_tm->tm_hour*60 + now_tm->tm_min;
	const int row_top[2] = {0, BOTTOM_Y - DATE_Y}, row_h[2] = {DATE_H, BOTTOM_H};
	// the battery is covered, the layer is above it:
	graphics_context_set_fill_color(ctx, COLOR_DATE_BKGR);
	graphics_fill_rect(ctx, GRect(0, BOTTOM_Y - DATE_Y, 200, BOTTOM_H), 0, GCornerNone);
	int count = strlen(departures)/6, d = 0;
	for (int row = 0; row < 2; row++){
		char texts[4][12], status[4];
		int widths[4], n = 0, w = -TRAIN_GAP;
		bool rex[4];
		while ((n < 4) && (d < count)){
			const char *p = departures + 6*d;
			int hour = (p[0] - '0')*10 + p[1] - '0', min = (p[2] - '0')*10 + p[3] - '0';
			if (hour*60 + min < t){ d++; continue; } // departed
			rex[n] = (p[4] == 'X');
			if (rex[n]) snprintf(texts[n], sizeof(texts[n]), "%d:%02d", hour, min);
			else snprintf(texts[n], sizeof(texts[n]), "%d:%02d%c", hour, min, (p[4] == 'S') ? 's' : p[4]);
			widths[n] = graphics_text_layout_get_content_size(texts[n], fonts_get_system_font(rex[n] ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD),GRect(0, 0, 200, 40), GTextOverflowModeFill, GTextAlignmentLeft).w;
			if (w + TRAIN_GAP + widths[n] > 200) break;
			w += TRAIN_GAP + widths[n];
			status[n++] = p[5];
			d++;
		}
		int x = (200 - w)/2;
		for (int i = 0; i < n; i++){
			// the digits are vertically centered in the row
			int top = rex[i] ? GOTHIC_28_TOP : GOTHIC_24_TOP, h = rex[i] ? GOTHIC_28_H : GOTHIC_24_H, y = TEXT_Y(row_top[row], row_h[row], top, h);
			graphics_context_set_text_color(ctx, (status[i] == 'D') ? GColorRed : (status[i] == 'C') ? GColorDarkGray : COLOR_DATE_TEXT);
			graphics_draw_text(ctx, texts[i], fonts_get_system_font(rex[i] ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD),GRect(x, y, widths[i] + 4, 40), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			if (status[i] == 'C'){
				graphics_context_set_stroke_color(ctx, GColorDarkGray);
				graphics_context_set_stroke_width(ctx, 2);
				graphics_draw_line(ctx, GPoint(x, y + top + h/2), GPoint(x + widths[i], y + top + h/2));
			}
			x += widths[i] + TRAIN_GAP;
		}
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
	if (persist_exists(KEY_TRAIN_DEPARTURES)) persist_read_string(KEY_TRAIN_DEPARTURES, departures, sizeof(departures));
	if (persist_exists(KEY_TRAIN_LAST_UPDATE)) train_last_updated = (time_t)persist_read_int(KEY_TRAIN_LAST_UPDATE);
	if (persist_exists(KEY_TRAIN_DONE)) train_done = persist_read_int(KEY_TRAIN_DONE);
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
	persist_write_string(KEY_TRAIN_DEPARTURES, departures);
	persist_write_int(KEY_TRAIN_LAST_UPDATE, (int)train_last_updated);
	persist_write_int(KEY_TRAIN_DONE, train_done);
}

// time since the last weather update, red if older than the update interval; the weather is hidden if it is older than WEATHER_HIDE_AFTER_MINUTES
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
	bool outdated = (phone_last_updated == 0) || (age > WEATHER_HIDE_AFTER_MINUTES*60);
	if (outdated != weather_outdated){
		weather_outdated = outdated;
		layer_set_hidden(text_layer_get_layer(s_condition_layer), outdated);
		layer_set_hidden(text_layer_get_layer(s_temp_layer), outdated);
		layer_set_hidden(text_layer_get_layer(s_temp_min_max_layer), outdated);
		layer_set_hidden(s_rain_layer, outdated);
		layer_mark_dirty(s_icon_layer);
		layer_mark_dirty(s_background_layer);
	}
}

static void display_weather(void) {
	snprintf(temp_text, sizeof(temp_text), "%d°C", weather_temp);
	// e.g. "-12°C" does not fit beside the rain indicator in size 36, then size 32 is used
	bool fits = graphics_text_layout_get_content_size(temp_text, s_font_36, GRect(0, 0, 500, 60), GTextOverflowModeFill, GTextAlignmentLeft).w <= 200 - WEATHER_ICON_SIZE - RAIN_WIDTH;
	text_layer_set_font(s_temp_layer, fits ? s_font_36 : s_font_32);
	text_layer_set_text(s_temp_layer, temp_text);
	text_layer_set_text_color(s_temp_layer, get_temperature_color(weather_temp));
	temp_dy = fits ? 0 : TEXT_36_TOP - TEXT_32_TOP + (TEXT_36_H - TEXT_32_H)/2;
	text_layer_set_text(s_temp_min_max_layer, temp_min_max);

	// weather condition: in the font of the date, or smaller if it does not fit on one line (in the smallest one cut with "..."); the letters are centered without the descenders
	GFont fonts[3] = {s_font_22, s_font_18, s_font_14};
	static const int tops[3] = {TEXT_22_TOP, TEXT_18_TOP, TEXT_14_TOP};
	static const int heights[3] = {TEXT_22_H, TEXT_18_H, TEXT_14_H};
	int f = 0;
	GSize size;
	for (f = 0; f < 3; f++){
		size = graphics_text_layout_get_content_size(weather_condition, fonts[f], GRect(0, 0, 1000, 100), GTextOverflowModeFill, GTextAlignmentCenter);
		if ((size.w <= 200 - 2*PADDING) || (f == 2)) break;
	}
	text_layer_set_font(s_condition_layer, fonts[f]);
	text_layer_set_text(s_condition_layer, weather_condition);
	condition_y = TEXT_Y(CONDITION_Y, CONDITION_H, tops[f], heights[f]);
	condition_h = size.h + 4; // a single line
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
	layer_mark_dirty(s_icon_layer);
	layer_mark_dirty(s_rain_layer);
	layer_mark_dirty(s_background_layer);
}

// to the JS: the weather (key 0) or the departures (KEY_TRAIN_REQUEST); false if the outbox is busy, then it is tried again in the next minute
static bool send_request(uint32_t key, uint8_t value) {
	DictionaryIterator *iter;
	if (app_message_outbox_begin(&iter) != APP_MSG_OK) return false;
	dict_write_uint8(iter, key, value);
	return app_message_outbox_send() == APP_MSG_OK;
}

// the notification icons replace the sleep if there are any; the departures cover both
static void update_bottom_row(void) {
	layer_set_hidden(text_layer_get_layer(s_sleep_layer), trains_shown || (notif_icons[0] > 0));
	layer_set_hidden(s_notif_layer, notif_icons[0] == 0);
	layer_mark_dirty(s_notif_layer);
}

// the departures replace the date and the sleep on the train days, from the start time until the JS reports the morning / evening as over, or the end time
static void update_train_mode(void) {
	time_t now = time(NULL);
	struct tm now_tm = *localtime(&now);
	int t = now_tm.tm_hour*60 + now_tm.tm_min;
	int mode = 0;
	if (TRAIN_WEEKDAYS & (1 << now_tm.tm_wday)){
		if ((t >= TRAIN_MORNING_START) && (t < TRAIN_MORNING_END)) mode = 1;
		if ((t >= TRAIN_EVENING_START) && (t < TRAIN_EVENING_END)) mode = 2;
	}
	if (train_done == now_tm.tm_yday*10 + mode) mode = 0;
	// departures from before the start of this morning / evening (e.g. of yesterday) are dropped:
	int start = (mode == 1) ? TRAIN_MORNING_START : TRAIN_EVENING_START;
	if (mode && (train_last_updated < now - (t - start)*60 - now_tm.tm_sec)) departures[0] = 0;
	if (mode != train_mode){
		train_mode = mode;
		train_last_request = 0; // request the departures right away
	}
	// only when there are departures still to come, else (e.g. before the first ones are received) the date and the sleep stay
	bool shown = false;
	for (int d = 0; mode && !shown && (d < (int)strlen(departures)/6); d++){
		const char *p = departures + 6*d;
		shown = ((p[0] - '0')*600 + (p[1] - '0')*60 + (p[2] - '0')*10 + p[3] - '0' >= t);
	}
	if (shown != trains_shown){
		trains_shown = shown;
		layer_set_hidden(text_layer_get_layer(s_date_layer), shown);
		update_bottom_row();
		layer_set_hidden(s_trains_layer, !shown);
	}
	layer_mark_dirty(s_trains_layer);
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
	update_train_mode();

	if (init_done && (do_update_weather || (time(NULL) - phone_last_updated >= WEATHER_UPDATE_INTERVAL_MINUTES*60-60)) && send_request(0, 0)) do_update_weather = false;
	if (init_done && train_mode && (time(NULL) - train_last_request >= TRAIN_UPDATE_INTERVAL_MINUTES*60) && send_request(KEY_TRAIN_REQUEST, train_mode)) train_last_request = time(NULL);
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
	if (!connected){ // the icons would be outdated
		notif_icons[0] = 0;
		update_bottom_row();
	}
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
			case KEY_TRAIN_DEPARTURES:
				snprintf(departures, sizeof(departures), "%s", t->value->cstring);
				train_last_updated = time(NULL);
				break;
			case KEY_NOTIF_ICONS: {
				memset(notif_icons, 0, sizeof(notif_icons));
				memcpy(notif_icons, t->value->data, (t->length < sizeof(notif_icons)) ? t->length : sizeof(notif_icons));
				int complete = (t->length - 1)/NOTIF_ICON_BYTES;
				if (notif_icons[0] > complete) notif_icons[0] = complete;
				update_bottom_row();
				break;
			}
			case KEY_TRAIN_DONE: {
				time_t now = time(NULL);
				train_done = localtime(&now)->tm_yday*10 + (int)t->value->int32;
				break;
			}
		}
	}
	save_data();
	display_weather();
	update_icon();
	update_train_mode();
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

	s_font_14  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_14));
	s_font_16  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_16));
	s_font_16r = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_REGULAR_16));
	s_font_18  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_18));
	s_font_18sb = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_SEMIBOLD_18));
	s_font_22  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_22));
	s_font_22sb = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_SEMIBOLD_22));
	s_font_24  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_24));
	s_font_32  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_32));
	s_font_36  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_36));

	s_temp_layer         = create_text_layer(s_font_36, COLOR_WEATHER_TEXT);
	s_location_layer    = create_text_layer(s_font_16, COLOR_HEADER_TEXT);
	s_last_update_layer  = create_text_layer(s_font_16r, COLOR_HEADER_TEXT);
	s_condition_layer    = create_text_layer(s_font_22, COLOR_CONDITION_TEXT);
	s_temp_min_max_layer = create_text_layer(s_font_22, COLOR_WEATHER_TEXT);
	s_date_layer         = create_text_layer(s_font_22sb, COLOR_DATE_TEXT);
	s_sleep_layer        = create_text_layer(s_font_18sb, COLOR_DATE_TEXT);
	s_battery_text_layer = create_text_layer(s_font_16, GColorIslamicGreen);
	text_layer_set_overflow_mode(s_condition_layer, GTextOverflowModeTrailingEllipsis);

	// after the battery text, so that the effect inverts it too:
	s_battery_fill_layer = effect_layer_create(GRectZero);
	effect_layer_add_effect(s_battery_fill_layer, effect_invert_color, (void *)0b00000000); //use global inverter color
	layer_add_child(s_window_layer, effect_layer_get_layer(s_battery_fill_layer));

	s_notif_layer = layer_create(GRectZero);
	layer_set_update_proc(s_notif_layer, notif_update_proc);
	layer_set_hidden(s_notif_layer, true);
	layer_add_child(s_window_layer, s_notif_layer);

	s_trains_layer = layer_create(GRectZero);
	layer_set_update_proc(s_trains_layer, trains_update_proc);
	layer_set_hidden(s_trains_layer, true);
	layer_add_child(s_window_layer, s_trains_layer);

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
	layer_destroy(s_trains_layer);
	layer_destroy(s_notif_layer);
	effect_layer_destroy(s_battery_fill_layer);
	text_layer_destroy(s_temp_layer);
	text_layer_destroy(s_location_layer);
	text_layer_destroy(s_last_update_layer);
	text_layer_destroy(s_condition_layer);
	text_layer_destroy(s_temp_min_max_layer);
	text_layer_destroy(s_date_layer);
	text_layer_destroy(s_sleep_layer);
	text_layer_destroy(s_battery_text_layer);
	GFont fonts[10] = {s_font_14, s_font_16, s_font_16r, s_font_18, s_font_18sb, s_font_22, s_font_22sb, s_font_24, s_font_32, s_font_36};
	for (int i = 0; i < 10; i++) fonts_unload_custom_font(fonts[i]);
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
