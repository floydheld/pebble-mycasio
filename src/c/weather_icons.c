#include "weather_icons.h"

// Vector weather icons replacing the Climacons font (whose glyphs are limited in size by the firmware).
// Icon codes are the Climacons characters, sent by the JS: '!' cloud, '"' cloud_day, then the first (plain) icons of the groups of 3 from '!' on:
//   '$' rain, "'" showers, '*' downpour, '-' drizzle, '0' sleet, '9' snow, '<' fog, '?' haze; and 'B' wind, 'F' lightning, 'I' sun, 'X' tornado

// The icons are designed for a 72x72 box, S() scales a length and P() a point of that design to WEATHER_ICON_SIZE.
#define S(v) ((v) * WEATHER_ICON_SIZE / 72)
#define P(x, y) GPoint(S(x), S(y))

// c is already scaled, r is a design length
static GPoint polar(GPoint c, int r, int32_t angle) {
	return GPoint(c.x + sin_lookup(angle) * S(r) / TRIG_MAX_RATIO, c.y - cos_lookup(angle) * S(r) / TRIG_MAX_RATIO);
}

// cloud spans x 10..64 and y 13+dy..50+dy; grow makes it bigger, used to cut a gap into a sun behind it
static void draw_cloud(GContext *ctx, int dy, int grow, GColor color) {
	graphics_context_set_fill_color(ctx, color);
	graphics_fill_circle(ctx, P(22, 38+dy), S(12+grow));
	graphics_fill_circle(ctx, P(37, 29+dy), S(16+grow));
	graphics_fill_circle(ctx, P(52, 38+dy), S(12+grow));
	graphics_fill_rect(ctx, GRect(S(22), S(34+dy-grow), S(53)-S(22), S(17+2*grow)), 0, GCornerNone);
}

static void draw_sun(GContext *ctx, GPoint c, int r, int ray, GColor color) {
	graphics_context_set_fill_color(ctx, color);
	graphics_context_set_stroke_color(ctx, color);
	graphics_context_set_stroke_width(ctx, 3);
	graphics_fill_circle(ctx, c, S(r));
	for (int i = 0; i < 8; i++) {
		int32_t a = TRIG_MAX_ANGLE * i / 8;
		graphics_draw_line(ctx, polar(c, r+4, a), polar(c, r+4+ray, a));
	}
}

static void draw_snowflake(GContext *ctx, GPoint c, int s) {
	for (int i = 0; i < 3; i++) {
		int32_t a = TRIG_MAX_ANGLE * i / 6;
		graphics_draw_line(ctx, polar(c, s, a), polar(c, s, a + TRIG_MAX_ANGLE/2));
	}
}

void weather_icon_draw(GContext *ctx, int icon, GColor fg, GColor bg) {
	graphics_context_set_antialiased(ctx, true);
	graphics_context_set_stroke_color(ctx, fg);
	graphics_context_set_fill_color(ctx, fg);
	graphics_context_set_stroke_width(ctx, 3);

	if (icon == 'B') return; // wind only, the wind lines are drawn on top
	if (icon == 'I') {
		draw_sun(ctx, P(36, 36), 15, 9, fg);
		return;
	}
	if (icon == 'X') { // tornado: horizontal lines getting narrower downwards
		graphics_context_set_stroke_width(ctx, 4);
		for (int i = 0; i < 6; i++) {
			int w = 28 - i*5, x = 36 + ((i % 2) ? 3 : -3), y = 10 + i*10;
			graphics_draw_line(ctx, P(x-w, y), P(x+w, y));
		}
		return;
	}
	int group = (icon == 'F') ? 12 : (icon - '!') / 3;
	if (group == 10) { // haze: sun above horizontal lines
		draw_sun(ctx, P(36, 24), 11, 6, fg);
		graphics_context_set_stroke_color(ctx, fg);
		graphics_context_set_stroke_width(ctx, 3);
		graphics_draw_line(ctx, P(12, 50), P(60, 50));
		graphics_draw_line(ctx, P(18, 57), P(66, 57));
		graphics_draw_line(ctx, P( 8, 64), P(56, 64));
		return;
	}

	bool precip = (group != 0);
	int dy = precip ? -3 : ((icon == '"') ? 8 : 4);
	if (icon == '"') { // small sun behind the cloud, with a gap around the cloud
		draw_sun(ctx, P(52, 18), 9, 5, fg);
		draw_cloud(ctx, dy, 3, bg);
	}
	draw_cloud(ctx, dy, 0, fg);

	int y0 = 54 + dy; // top of the precipitation
	static const int xs[4] = {20, 31, 42, 53};
	graphics_context_set_stroke_color(ctx, fg);
	graphics_context_set_fill_color(ctx, fg);
	graphics_context_set_stroke_width(ctx, 3);
	switch (group) {
		case 1: // rain
			for (int i = 0; i < 4; i++) graphics_draw_line(ctx, P(xs[i]+3, y0), P(xs[i]-3, y0+12));
			break;
		case 2: // showers
			for (int i = 0; i < 4; i++) {
				graphics_draw_line(ctx, P(xs[i]+2, y0), P(xs[i], y0+4));
				graphics_draw_line(ctx, P(xs[i]-1, y0+9), P(xs[i]-3, y0+13));
			}
			break;
		case 3: // downpour
			graphics_context_set_stroke_width(ctx, 4);
			for (int x = 16; x <= 56; x += 10) graphics_draw_line(ctx, P(x+4, y0), P(x-4, y0+15));
			break;
		case 4: // drizzle
			for (int i = 0; i < 4; i++) {
				graphics_fill_circle(ctx, P(xs[i], y0+2), S(2));
				graphics_fill_circle(ctx, P(xs[i]-4, y0+10), S(2));
			}
			break;
		case 5: // sleet: rain and snow
			for (int i = 0; i < 4; i++) {
				if (i % 2) graphics_draw_line(ctx, P(xs[i]+3, y0), P(xs[i]-3, y0+12));
				else {
					graphics_fill_circle(ctx, P(xs[i], y0+3), S(3));
					graphics_fill_circle(ctx, P(xs[i]-2, y0+11), S(3));
				}
			}
			break;
		case 8: // snow
			graphics_context_set_stroke_width(ctx, 2);
			draw_snowflake(ctx, P(18, y0+5), 6);
			draw_snowflake(ctx, P(36, y0+9), 6);
			draw_snowflake(ctx, P(54, y0+5), 6);
			break;
		case 9: // fog
			graphics_draw_line(ctx, P(14, y0+1), P(58, y0+1));
			graphics_draw_line(ctx, P(20, y0+7), P(64, y0+7));
			graphics_draw_line(ctx, P(10, y0+13), P(50, y0+13));
			break;
		case 12: { // lightning
			GPoint bolt[7] = {P(39, y0-4), P(27, y0+8), P(35, y0+8), P(30, y0+18), P(47, y0+3), P(39, y0+3), P(44, y0-4)};
			GPath path = {.num_points = 7, .points = bolt, .rotation = 0, .offset = GPointZero};
			gpath_draw_filled(ctx, &path);
			break;
		}
	}
}

void moon_phase_draw(GContext *ctx, int phase, GColor lit, GColor dark) {
	// row by row: the terminator is at x = w*cos(phase angle), w = half width of the disc in that row
	const int r = S(32), cx = S(36), cy = S(36);
	int32_t cs = cos_lookup(TRIG_MAX_ANGLE * phase / 28);
	graphics_context_set_antialiased(ctx, false);
	graphics_context_set_stroke_width(ctx, 1);
	for (int y = -r; y <= r; y++) {
		int w = 0;
		while ((w+1)*(w+1) <= r*r - y*y) w++;
		int t = w * cs / TRIG_MAX_RATIO;
		graphics_context_set_stroke_color(ctx, dark);
		graphics_draw_line(ctx, GPoint(cx-w, cy+y), GPoint(cx+w, cy+y));
		graphics_context_set_stroke_color(ctx, lit);
		if ((phase <= 14) && (t < w)) graphics_draw_line(ctx, GPoint(cx+t, cy+y), GPoint(cx+w, cy+y)); // waxing: lit on the right
		if ((phase > 14) && (-w < -t)) graphics_draw_line(ctx, GPoint(cx-w, cy+y), GPoint(cx-t, cy+y)); // waning: lit on the left
	}
}

void wind_lines_draw(GContext *ctx, int count, GColor color, GColor outline) {
	// start and length of each line (in the 72px design), so that the lines are differently wide and offset to each other
	static const int starts[4] = {4, 0, 16, 6}, lengths[4] = {44, 30, 38, 32}; // ends at 48, 30, 54, 38: the curls of neighbouring lines are apart
	graphics_context_set_antialiased(ctx, true);
	// first all outlines, then the lines
	for (int pass = 0; pass < 2; pass++) {
		graphics_context_set_stroke_color(ctx, pass ? color : outline);
		graphics_context_set_stroke_width(ctx, pass ? 2 : 4);
		for (int i = 0; i < count; i++) {
			// one smooth wave period, ending in a round curl upwards
			GPoint pts[40];
			int n = 0;
			// curl and wave smaller with more lines, so that they do not touch the line above
			int gap = WEATHER_ICON_SIZE / (count+1), y = WEATHER_ICON_SIZE * (i+1) / (count+1), x0 = S(starts[i]), len = S(lengths[i]);
			int r = (S(7) < 2*gap/5) ? S(7) : 2*gap/5, amp = (S(5) < gap/4) ? S(5) : gap/4;
			for (int k = 0; k <= 24; k++) pts[n++] = GPoint(x0 + k*len/24, y - amp * sin_lookup(TRIG_MAX_ANGLE * k / 24) / TRIG_MAX_RATIO);
			GPoint c = GPoint(x0 + len, y-r);
			for (int k = 1; k <= 12; k++) {
				int32_t a = TRIG_MAX_ANGLE/2 - k * TRIG_MAX_ANGLE * 3 / 48; // from the bottom of the circle upwards, 22.5 degree steps, 270 degree in total
				pts[n++] = GPoint(c.x + sin_lookup(a) * r / TRIG_MAX_RATIO, c.y - cos_lookup(a) * r / TRIG_MAX_RATIO);
			}
			for (int k = 1; k < n; k++) graphics_draw_line(ctx, pts[k-1], pts[k]);
		}
	}
}

void bt_disconnected_draw(GContext *ctx, GColor fg, GColor strike) {
	// bluetooth rune, a = half width, the two diagonals cross in the middle:
	int h = WEATHER_ICON_SIZE - 20, a = h/4, cx = WEATHER_ICON_SIZE/2, y0 = 10, y1 = y0 + h;
	graphics_context_set_antialiased(ctx, true);
	graphics_context_set_stroke_width(ctx, 4);
	graphics_context_set_stroke_color(ctx, fg);
	graphics_draw_line(ctx, GPoint(cx, y0), GPoint(cx, y1));
	graphics_draw_line(ctx, GPoint(cx, y0), GPoint(cx+a, y0+a));
	graphics_draw_line(ctx, GPoint(cx+a, y0+a), GPoint(cx-a, y1-a));
	graphics_draw_line(ctx, GPoint(cx, y1), GPoint(cx+a, y1-a));
	graphics_draw_line(ctx, GPoint(cx+a, y1-a), GPoint(cx-a, y0+a));
	graphics_context_set_stroke_color(ctx, strike);
	graphics_draw_line(ctx, GPoint(cx-h/2, y0), GPoint(cx+h/2, y1));
}
