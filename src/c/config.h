// Fixed settings (there is no config page any more)

// default location until the first weather update (BERLIN), longitude is positive for East and negative for West
#define LATITUDE   52.519444

#define LIGHT_WHEN_PLUGGED  1 // backlight on while plugged in and fully charged
#define VIBE_ON_DISCONNECT  0 // vibrate on bluetooth disconnect / reconnect
#define VIBE_ON_HOUR        0 // vibrate every full hour

#define WEATHER_UPDATE_INTERVAL_MINUTES 30
#define WEATHER_HIDE_AFTER_MINUTES      120 // the weather is hidden when it is older, e.g. without bluetooth for that long (the location and the age stay)

// ÖBB departures in the two bottom rows (instead of the date and the sleep) and a note instead of the weather condition, sent by the Android app (TrainService.kt, also the days and times):
// mornings from Pressbaum to Wien Westbahnhof until on the train, evenings in Vienna back to Pressbaum until on the train, and where the bike is until got off.
// They are shown until the Android app clears them or the departures are gone; only those of today.

// wind speed in km/h from which on 1, 2, 3, 4 wind lines are drawn over the weather icon (Beaufort 3, 4, 6, 8)
#define WIND_LINES_1 12
#define WIND_LINES_2 20
#define WIND_LINES_3 39
#define WIND_LINES_4 62
