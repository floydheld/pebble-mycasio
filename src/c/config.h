// Fixed settings (there is no config page any more)

// default location until the first weather update (BERLIN), longitude is positive for East and negative for West
#define LATITUDE   52.519444

#define LIGHT_WHEN_PLUGGED  1 // backlight on while plugged in and fully charged
#define VIBE_ON_DISCONNECT  0 // vibrate on bluetooth disconnect / reconnect
#define VIBE_ON_HOUR        0 // vibrate every full hour

#define WEATHER_UPDATE_INTERVAL_MINUTES 30
#define WEATHER_HIDE_AFTER_MINUTES      120 // the weather is hidden when it is older, e.g. without bluetooth for that long (the location and the age stay)

// ÖBB departures in the two bottom rows (instead of the date and the sleep) on the weekdays of TRAIN_WEEKDAYS (bit 0 = Sunday .. bit 6 = Saturday), times in minutes of the day
// mornings Pressbaum -> Wien Westbahnhof until leaving the area of Pressbaum, evenings Wien Westbahnhof -> Pressbaum until arriving there (area and stations: see index.js)
#define TRAIN_WEEKDAYS      0b0010110 // Monday, Tuesday, Thursday
#define TRAIN_MORNING_START (6*60 + 30)
#define TRAIN_MORNING_END   (10*60)   // at the latest, e.g. without a position from the phone
#define TRAIN_EVENING_START (16*60)
#define TRAIN_EVENING_END   (20*60)
#define TRAIN_UPDATE_INTERVAL_MINUTES 5

// wind speed in km/h from which on 1, 2, 3, 4 wind lines are drawn over the weather icon (Beaufort 3, 4, 6, 8)
#define WIND_LINES_1 12
#define WIND_LINES_2 20
#define WIND_LINES_3 39
#define WIND_LINES_4 62
