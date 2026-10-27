// Ignore "...is better written in dot notation" warning
// jshint -W069

var CLIMACON = {
  'cloud'            : '!',
  'cloud_day'        : '"',
  'rain'             : '$',
  'showers'          : "'",
  'downpour'         : '*',
  'drizzle'          : '-',
  'sleet'            : '0',
  'snow'             : '9',
  'fog'              : '<',
  'haze'             : '?',
  'wind'             : 'B',
  'lightning'        : 'F',
  'sun'              : 'I',
  'tornado'          : 'X'
};

var OWMclimacon= {
// Thunderstorm
  200 : CLIMACON['lightning'], // thunderstorm with light rain
  201 : CLIMACON['lightning'], // thunderstorm with rain
  202 : CLIMACON['lightning'], // thunderstorm with heavy rain
  210 : CLIMACON['lightning'], // light thunderstorm
  211 : CLIMACON['lightning'], // thunderstorm
  212 : CLIMACON['lightning'], // heavy thunderstorm
  221 : CLIMACON['lightning'], // ragged thunderstorm
  230 : CLIMACON['lightning'], // thunderstorm with light drizzle
  231 : CLIMACON['lightning'], // thunderstorm with drizzle
  232 : CLIMACON['lightning'], // thunderstorm with heavy drizzle
// Drizzle
  300 : CLIMACON['drizzle'], // light intensity drizzle
  301 : CLIMACON['drizzle'], // drizzle
  302 : CLIMACON['drizzle'], // heavy intensity drizzle
  310 : CLIMACON['drizzle'], // light intensity drizzle rain
  311 : CLIMACON['drizzle'], // drizzle rain
  312 : CLIMACON['drizzle'], // heavy intensity drizzle rain
  313 : CLIMACON['showers'], // shower rain and drizzle
  314 : CLIMACON['showers'], // heavy shower rain and drizzle
  321 : CLIMACON['showers'], // shower drizzle
// Rain
  500 : CLIMACON['rain'], // light rain
  501 : CLIMACON['rain'], // moderate rain
  502 : CLIMACON['downpour'], // heavy intensity rain
  503 : CLIMACON['downpour'], // very heavy rain
  504 : CLIMACON['downpour'], // extreme rain
  511 : CLIMACON['downpour'], // freezing rain
  520 : CLIMACON['showers'], // light intensity shower rain
  521 : CLIMACON['showers'], // shower rain
  522 : CLIMACON['showers'], // heavy intensity shower rain
  531 : CLIMACON['showers'], // ragged shower rain
// Snow
  600 : CLIMACON['snow'], // light snow
  601 : CLIMACON['snow'], // snow
  602 : CLIMACON['snow'], // heavy snow
  611 : CLIMACON['sleet'], // sleet
  612 : CLIMACON['sleet'], // shower sleet
  615 : CLIMACON['snow'], // light rain and snow
  616 : CLIMACON['snow'], // rain and snow
  620 : CLIMACON['snow'], // light shower snow
  621 : CLIMACON['snow'], // shower snow
  622 : CLIMACON['snow'], // heavy shower snow
// Atmosphere
  701 : CLIMACON['haze'], // mist
  711 : CLIMACON['haze'], // smoke
  721 : CLIMACON['haze'], // haze
  731 : CLIMACON['haze'], // Sand/Dust Whirls
  741 : CLIMACON['fog'], // Fog
  751 : CLIMACON['haze'], // sand
  761 : CLIMACON['haze'], // dust
  762 : CLIMACON['haze'], // VOLCANIC ASH
  771 : CLIMACON['wind'], // SQUALLS
  781 : CLIMACON['tornado'], // TORNADO
// Clouds
  800 : CLIMACON['sun'], // sky is clear
  801 : CLIMACON['cloud_day'], // few clouds
  802 : CLIMACON['cloud_day'], // scattered clouds
  803 : CLIMACON['cloud_day'], // broken clouds
  804 : CLIMACON['cloud'] // overcast clouds
};

var OWM_API_KEY = "1b5b37a3117bb6acd583d662fdbb24c7";
var OWM_LANG = "de";
var DEFAULT_LOCATION = "Vienna"; // used when the phone never delivered a position

var xhrRequest = function (url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(this.responseText);
  };
  xhr.open(type, url);
  xhr.send();
};

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      window.localStorage.setItem("last_location", JSON.stringify({lat: pos.coords.latitude, lon: pos.coords.longitude}));
      sendWeather(pos.coords.latitude, pos.coords.longitude, 0);
    },
    function(err) {
      console.log("location error (" + err.code + "): " + err.message);
      var last = window.localStorage.getItem("last_location");
      if (last) {
        last = JSON.parse(last);
        sendWeather(last.lat, last.lon, 0);
      } else {
        sendWeather(null, null, 2);
      }
    },
    {enableHighAccuracy: false, timeout: 10000, maximumAge: 0}
  );
}

// warn_location: 0 = ok, 2 = no position from the phone, default location used
function sendWeather(lat, lon, warn_location) {
  var query = (lat === null) ? "q=" + encodeURIComponent(DEFAULT_LOCATION) : "lat=" + (Math.round(lat*10000)/10000) + "&lon=" + (Math.round(lon*10000)/10000);
  var params = "?APPID=" + OWM_API_KEY + "&units=metric&lang=" + OWM_LANG + "&" + query;
  var url = "https://api.openweathermap.org/data/2.5/weather" + params;
  var url_forecast = "https://api.openweathermap.org/data/2.5/forecast" + params;

  xhrRequest(url_forecast, 'GET', function(forecastText) {
    xhrRequest(url, 'GET', function(weatherText) {
      var weather, forecast;
      try {
        weather = JSON.parse(weatherText);
        if (!weather.main) throw weatherText;
      } catch (e) {
        console.log("could not parse weather data: " + weatherText);
        Pebble.sendAppMessage({"KEY_WEATHER_STRING_2": "Fehler: Wetterdaten"});
        return;
      }

      // thunderstorm, drizzle, rain and snow have weather ids below 700
      var rain_in = (weather.weather[0].id < 700) ? 0 : -1;

      // min/max temperature of the next 24 hours (8 entries of the 3 hour forecast):
      var temp_min_max = " --/-- ";
      try {
        forecast = JSON.parse(forecastText);
        // next rain in the 5 day forecast: in hours if today, else in days (1 = tomorrow)
        var now = new Date();
        var today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        for (var j = 0; (rain_in < 0) && (j < forecast.list.length); j++) {
          if (forecast.list[j].weather[0].id < 700) {
            var t = new Date(forecast.list[j].dt*1000);
            var days = Math.round((new Date(t.getFullYear(), t.getMonth(), t.getDate()) - today) / 86400000);
            rain_in = (days === 0) ? Math.max(1, Math.round((t - now) / 3600000)) : 100 + days;
          }
        }
        var t_min = 1000, t_max = -1000;
        for (var i = 0; i < Math.min(forecast.list.length, 8); i++) {
          t_min = Math.min(forecast.list[i].main.temp, t_min);
          t_max = Math.max(forecast.list[i].main.temp, t_max);
        }
        // the degree sign is sent as "__" and replaced on the watch
        temp_min_max = Math.round(t_min) + "__/" + Math.round(t_max) + "__"; // min left, max right
      } catch (e) {
        console.log("could not parse forecast data: " + e);
      }

      var condition = weather.weather[0].description;
      condition = condition.charAt(0).toUpperCase() + condition.slice(1);

      var dictionary = {
        "KEY_LOCATION_NAME": weather.name,
        "KEY_LOCATION_LAT": Math.round(weather.coord.lat*1000000),
        "KEY_WEATHER_TEMP": Math.round(weather.main.temp),
        "KEY_WEATHER_STRING_1": temp_min_max,
        "KEY_WEATHER_STRING_2": condition,
        "KEY_WEATHER_ICON": (OWMclimacon[weather.weather[0].id] || CLIMACON['cloud']).charCodeAt(0),
        "KEY_WEATHER_WIND": Math.round(weather.wind.speed*3.6), // m/s -> km/h
        "KEY_WEATHER_RAIN": rain_in,
        "KEY_SUN_RISE_UNIX": weather.sys.sunrise,
        "KEY_SUN_SET_UNIX": weather.sys.sunset,
        "KEY_WARN_LOCATION": warn_location
      };
      console.log("Sending weather: " + JSON.stringify(dictionary));
      Pebble.sendAppMessage(dictionary,
        function(e) { console.log("Weather info sent to Pebble successfully!"); },
        function(e) { console.log("Error sending weather info to Pebble!"); }
      );
    });
  });
}

Pebble.addEventListener('ready', function(e) {
  console.log("PebbleKit JS ready!");
});

// the watch requests the weather (the departures come from the Android app)
Pebble.addEventListener('appmessage', function(e) {
  getWeather();
});
