#include <pebble.h>
#include "effects.h"
  
  
// set pixel color at given coordinates 
void set_pixel(uint8_t *bitmap_data, int bytes_per_row, int y, int x, uint8_t color) {
      
    bitmap_data[y*bytes_per_row + x] = color; // in Basalt - simple set entire byte
}

// get pixel color at given coordinates 
uint8_t get_pixel(uint8_t *bitmap_data, int bytes_per_row, int y, int x) {
  
    return bitmap_data[y*bytes_per_row + x]; // in Basalt - simple get entire byte
}
 

// inverter effect (black (or given color) -> given color -> black -> ...).
void effect_invert_color(GContext* ctx,  GRect position, void* param) {
  //capturing framebuffer bitmap
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  uint8_t *bitmap_data =  gbitmap_get_data(fb);
  int bytes_per_row = gbitmap_get_bytes_per_row(fb);

  uint8_t InverterColor = (uintptr_t)param;
  uint8_t BkgColor = 0;
  if (InverterColor == 0){
    InverterColor = GlobalInverterColor;
    BkgColor = GlobalBkgColor;
  }
  
  for (int y = 0; y < position.size.h; y++)
     for (int x = 0; x < position.size.w; x++)
          if ((get_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x) & 0b00111111) != BkgColor){
            set_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x, BkgColor);
          } else {
            set_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x, InverterColor);
          }
          
 
  graphics_release_frame_buffer(ctx, fb);
          
}
