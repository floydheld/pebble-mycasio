// Keys of the app messages from the JS (must match messageKeys in package.json), also used as persistent storage keys
#define KEY_LOCATION_NAME       0
#define KEY_LOCATION_LAT        1
#define KEY_WEATHER_TEMP        3
#define KEY_WEATHER_ICON        4
#define KEY_WEATHER_STRING_1    8 // min/max temperature
#define KEY_WEATHER_STRING_2    9 // weather condition
#define KEY_TIME_LAST_UPDATE   11 // persistent only
#define KEY_BTY_LAST_STATE     18 // persistent only
#define KEY_SUN_RISE_UNIX      40
#define KEY_SUN_SET_UNIX       41
#define KEY_WARN_LOCATION      50
#define KEY_WEATHER_WIND      110 // wind speed in km/h
#define KEY_WEATHER_RAIN      111 // next rain: -1 = none in sight, 0 = now, 1..23 = in hours (today), 100+d = in d days
#define KEY_TRAIN_DEPARTURES  120 // per departure 6 characters: time HHMM (incl. delay), S = S-Bahn / X = REX / ..., 0 = on time / D = delayed / C = cancelled
#define KEY_TRAIN_DONE        121 // the JS: the morning (1) / evening (2) is over (left / arrived in the area of Pressbaum); persistent: tm_yday*10 + 1 / 2
#define KEY_TRAIN_REQUEST     122 // to the JS: departures of the morning (1) / evening (2)
#define KEY_TRAIN_LAST_UPDATE 123 // persistent only
#define KEY_NOTIF_ICONS       130 // from the Android companion app (android/): count, then per icon NOTIF_ICON_SIZE x NOTIF_ICON_SIZE bits row by row (most significant bit first), 1 = opaque
#define KEY_PHONE_BATTERY     131 // from the Android companion app: battery level of the phone in percent
