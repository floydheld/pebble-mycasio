#include "seven_segment.h"

/*
    ----   <-- 1
  |      | <-- 3
  | <-- 2|
    ----   <-- 4
  | <-- 5|
  |      | <-- 6
    ----   <-- 7
*/

// bit (n-1) set = segment n is on
static const uint8_t s_digit_segments[10] = {
	0b1110111, // 0: 1 2 3 5 6 7
	0b0100100, // 1: 3 6
	0b1011101, // 2: 1 3 4 5 7
	0b1101101, // 3: 1 3 4 6 7
	0b0101110, // 4: 2 3 4 6
	0b1101011, // 5: 1 2 4 6 7
	0b1111011, // 6: 1 2 4 5 6 7
	0b0100101, // 7: 1 3 6
	0b1111111, // 8
	0b1101111, // 9: 1 2 3 4 6 7
};

// The segments of the original 26x41 pixel design (trapezoids, pointed middle segment) as polygons in pixel edge coordinates,
// they are scaled to the size of the digit. A point with x = -1 ends a polygon.
static const int8_t s_segment_points[7][8][2] = {
	{{2, 0}, {24, 0}, {19, 6}, {7, 6}, {-1, -1}},                                 // 1 top
	{{0, 2}, {6, 8}, {6, 17}, {3, 20}, {0, 18}, {-1, -1}},                        // 2 upper left
	{{26, 2}, {26, 18}, {23, 20}, {20, 17}, {20, 8}, {-1, -1}},                   // 3 upper right
	{{6, 18}, {20, 18}, {22, 20}, {22, 22}, {20, 24}, {6, 24}, {4, 22}, {4, 20}}, // 4 middle
	{{0, 24}, {3, 22}, {6, 25}, {6, 33}, {0, 39}, {-1, -1}},                      // 5 lower left
	{{26, 24}, {26, 39}, {20, 33}, {20, 25}, {23, 22}, {-1, -1}},                 // 6 lower right
	{{7, 35}, {19, 35}, {24, 41}, {2, 41}, {-1, -1}},                             // 7 bottom
};

void seven_segment_paint(GContext* ctx, int digit, GRect bounds) {
	if ((digit < 0) || (digit > 9)) return;
	for (int seg = 0; seg < 7; seg++) {
		if (!(s_digit_segments[digit] & (1 << seg))) continue;
		GPoint pts[8];
		int n = 0;
		while ((n < 8) && (s_segment_points[seg][n][0] >= 0)) {
			pts[n] = GPoint(bounds.origin.x + s_segment_points[seg][n][0] * bounds.size.w / SEVEN_SEGMENT_DESIGN_W, bounds.origin.y + s_segment_points[seg][n][1] * bounds.size.h / SEVEN_SEGMENT_DESIGN_H);
			n++;
		}
		GPath path = {.num_points = n, .points = pts, .rotation = 0, .offset = GPointZero};
		gpath_draw_filled(ctx, &path);
	}
}
