package pebble.mycasio.notifications

import android.Manifest
import android.annotation.SuppressLint
import android.app.AlarmManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.location.LocationListenerCompat
import androidx.core.location.LocationManagerCompat
import androidx.core.location.LocationRequestCompat
import com.google.android.gms.location.ActivityRecognition
import com.google.android.gms.location.ActivityTransition
import com.google.android.gms.location.ActivityTransitionEvent
import com.google.android.gms.location.ActivityTransitionRequest
import com.google.android.gms.location.ActivityTransitionResult
import com.google.android.gms.location.DetectedActivity
import io.rebble.pebblekit2.client.DefaultPebbleSender
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.TransmissionResult
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.plus
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.time.DayOfWeek
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.LocalTime
import java.time.ZoneId

const val KEY_TRAIN_DEPARTURES = 120u // messageKeys in the watchface's package.json
const val KEY_TRAIN_NOTE = 124u

class Station(val name: String, val id: String, val lat: Double, val lon: Double) {
    fun distance(fix: Location) = FloatArray(1).also { Location.distanceBetween(lat, lon, fix.latitude, fix.longitude, it) }[0]
}

val PRESSBAUM = Station("Pressbaum", "1132415", 48.181354, 16.077837)
val TULLNERBACH = Station("Tullnerbach", "1132421", 48.186065, 16.093289) // Tullnerbach-Pressbaum
val WESTBAHNHOF = Station("Westbahnhof", "1291501", 48.196870, 16.337607)
val HOME_STATIONS = listOf(PRESSBAUM, TULLNERBACH)

val TRAIN_DAYS = setOf(DayOfWeek.MONDAY, DayOfWeek.TUESDAY, DayOfWeek.THURSDAY)
val MORNING = LocalTime.of(6, 30) to LocalTime.of(10, 0) // the ends at the latest, e.g. without a position
val EVENING = LocalTime.of(16, 0) to LocalTime.MAX // the end only because the departures are times of the day
const val UPDATE_MINUTES = 5L    // the departures
const val STATION_RADIUS = 300f  // m: at a station
const val NEAR_RADIUS = 1000f    // m: in the morning GPS within this distance of the stations of Pressbaum, else the power saving location
const val VIENNA_RADIUS = 12000f // m: Vienna around Westbahnhof: the evening departures only there
const val LEFT_HOME = 250f       // m: in the morning GPS only after moving this far from the first position (not at home)
const val AREA_RADIUS = 5000f    // m: the area of Pressbaum around PRESSBAUM: leaving it in the morning = on the train; in it for HOME_MS in the evening = home
const val ACCURATE = 50f         // m: positions at least this accurate count for the speed
const val TRACK_ACCURACY = 150f  // m: positions at least this accurate are kept for the station of the bike (the stations are 1.3 km apart)
const val WALKING_KMH = 10f      // slower = not on the bike or the train any more
const val BIKE_RADIUS = 500f     // m: the bike is at the station within this distance of where the ride ended
const val TRACK_MAX_AGE_MS = 5 * 60_000L // a position older than this before the end of the ride does not tell where it ended
const val HOME_MS = 15 * 60_000L // evening: the note stays until in the area of Pressbaum this long
const val PREF_TRAIN_DONE = "train_done" // "2026-10-15/1": the morning (1) / evening (2) of that day is over
const val PREF_BIKE = "bike_station"     // name of the station of the bike, or BIKE_HOME
const val BIKE_HOME = "daheim"           // walked to the train: no bike ride this morning
const val PREF_BIKE_DATE = "bike_date"   // the day when it was left there

// 1 = morning, 2 = evening, 0 = no trains now (also when that morning / evening is over)
fun trainMode(context: Context, now: LocalDateTime = LocalDateTime.now()): Int {
    if (now.dayOfWeek !in TRAIN_DAYS) return 0
    val t = now.toLocalTime()
    val mode = if (t >= MORNING.first && t < MORNING.second) 1 else if (t >= EVENING.first && t < EVENING.second) 2 else 0
    return if (context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(PREF_TRAIN_DONE, null) == "${now.toLocalDate()}/$mode") 0 else mode
}

// where the bike was left this morning (a station name or BIKE_HOME), null if unknown
fun bikeAt(context: Context): String? {
    val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    return if (prefs.getString(PREF_BIKE_DATE, null) == LocalDate.now().toString()) prefs.getString(PREF_BIKE, null) else null
}

// exact (on time also when the phone sleeps), unless denied in the settings (Android 12)
fun setAlarm(alarms: AlarmManager, type: Int, at: Long, intent: PendingIntent) {
    if (Build.VERSION.SDK_INT < 31 || alarms.canScheduleExactAlarms()) alarms.setExactAndAllowWhileIdle(type, at, intent)
    else alarms.setAndAllowWhileIdle(type, at, intent)
}

// wakes TrainReceiver: every UPDATE_MINUTES while TrainService runs, else at the next start of the trains (now if they are due)
fun scheduleTrains(context: Context, running: Boolean) {
    val now = LocalDateTime.now()
    val next = when {
        running -> now.plusMinutes(UPDATE_MINUTES)
        trainMode(context, now) != 0 -> now
        else -> generateSequence(now.toLocalDate()) { it.plusDays(1) }.take(8).filter { it.dayOfWeek in TRAIN_DAYS }.flatMap { sequenceOf(it.atTime(MORNING.first), it.atTime(EVENING.first)) }.first { it > now }
    }
    val intent = PendingIntent.getBroadcast(context, 1, Intent(context, TrainReceiver::class.java), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    setAlarm(context.getSystemService(AlarmManager::class.java), AlarmManager.RTC_WAKEUP, next.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli(), intent)
}

// starts TrainService (or lets it update the departures) while the trains are due, else waits for the next time
class TrainReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (trainMode(context) == 0 && TrainService.instance == null) return scheduleTrains(context, false)
        // without it startForeground fails, and stopping before it crashes the app ("startForegroundService() did not then call startForeground()")
        val located = context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED && (Build.VERSION.SDK_INT < 29 || context.checkSelfPermission(Manifest.permission.ACCESS_BACKGROUND_LOCATION) == PackageManager.PERMISSION_GRANTED)
        if (!located && TrainService.instance == null) {
            TrainService.status = "not started: no location access all the time"
            return scheduleTrains(context, true)
        }
        try {
            context.startForegroundService(Intent(context, TrainService::class.java))
        } catch (e: Exception) { // e.g. not allowed from the background without the exact alarms
            TrainService.status = "could not start: $e"
            scheduleTrains(context, true)
        }
    }
}

// The activity transitions of the morning from Google Play services (see TrainService.requestTransitions)
class TransitionReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val result = ActivityTransitionResult.extractResult(intent) ?: return
        TrainService.instance?.onTransitions(result.transitionEvents)
    }
}

// The ÖBB departures and a note on the watchface, and where the bike is:
// mornings from Pressbaum to Wien Westbahnhof until on the train: Google's activity recognition reports riding in a vehicle (IN_VEHICLE), or out of the area of Pressbaum.
// The bike is at the station (Pressbaum or Tullnerbach-Pressbaum) where the last bike ride ended (ON_BICYCLE exit), else where the train was boarded (see stationAt);
// at home (BIKE_HOME) if there was no bike ride (walked to the train).
// Evenings while in Vienna from Wien Westbahnhof to Pressbaum; once in Vienna the note "Rad: <station>" until in the area of Pressbaum for HOME_MS.
// A foreground service with location updates (GPS only near the stations in the morning); TrainReceiver starts it and wakes it every UPDATE_MINUTES.
class TrainService : Service() {
    private val scope = MainScope() + CoroutineExceptionHandler { _, e -> lastSend = "error: $e" } // e.g. the Pebble app fails: not a crash
    private var mode = 0
    private var inVienna = false               // evening: the departures only there
    private var wasInVienna = false            // evening: the note from then on
    private var homeSince: Long? = null        // evening: in the area of Pressbaum since (ms of elapsedRealtime)
    private var departures = ""                // see KEY_TRAIN_DEPARTURES
    private var fetching: Job? = null
    private var first: Location? = null        // morning: the first position (at home)
    private var last: Location? = null
    private val track = ArrayList<Pair<Location, Float?>>() // morning: the positions (TRACK_ACCURACY) with their speed in km/h (null = unknown)
    private var rode = false                   // morning: a bike ride was recognized (else walked to the train, the bike is at home)
    private var bikeParked = false             // morning: the last bike ride ended at a station (else the bike is where the train was boarded)
    private var precise: Boolean? = null       // GPS or the power saving location, null = none requested
    private val listener = LocationListenerCompat { onLocation(it) }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        instance = this
    }

    override fun onDestroy() {
        instance = null
        LocationManagerCompat.removeUpdates(getSystemService(LocationManager::class.java), listener)
        requestTransitions(false)
        scope.cancel()
        super.onDestroy()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        getSystemService(NotificationManager::class.java).createNotificationChannel(NotificationChannel("trains", "Trains", NotificationManager.IMPORTANCE_MIN))
        val notification = Notification.Builder(this, "trains").setSmallIcon(android.R.drawable.ic_menu_mylocation).setContentTitle("Trains and bike").build()
        try {
            ServiceCompat.startForeground(this, 1, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION)
        } catch (e: Exception) { // no location permission (all the time)
            status = "could not start: $e"
            stopSelf()
            scheduleTrains(this, true)
            return START_NOT_STICKY
        }
        val mode = trainMode(this)
        if (mode == 0) {
            finish(false)
            return START_NOT_STICKY
        }
        if (mode != this.mode) {
            this.mode = mode
            departures = ""
            inVienna = false
            wasInVienna = false
            homeSince = null
            first = null
            last = null
            track.clear()
            rode = false
            bikeParked = false
            activities = ""
            precise = null
            requestLocation(false)
            requestTransitions(mode == 1)
        }
        update()
        scheduleTrains(this, true)
        return START_NOT_STICKY
    }

    // fetches the departures (if they are shown) and sends them with the note
    private fun update() {
        fetching?.cancel()
        fetching = scope.launch {
            val wakeLock = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MyCasio:trains").apply { acquire(60_000) }
            try {
                val shown = mode == 1 || inVienna
                departures = if (!shown) "" else withContext(Dispatchers.IO) { if (mode == 1) fetchDepartures(PRESSBAUM, WESTBAHNHOF) else fetchDepartures(WESTBAHNHOF, PRESSBAUM) } ?: departures
                send()
            } finally {
                if (wakeLock.isHeld) wakeLock.release()
            }
        }
    }

    // like the departure board (ÖBB Scotty), see KEY_TRAIN_DEPARTURES; null on errors
    private fun fetchDepartures(from: Station, to: Station): String? = try {
        val url = URL("https://fahrplan.oebb.at/bin/stboard.exe/dn?L=vs_scotty.vs_liveticker&evaId=${from.id}&dirInput=${to.id}&boardType=dep&productsFilter=1111111111111&tickerID=dep&start=yes&eqstops=false&maxJourneys=10&additionalTime=0&outputMode=tickerDataOnly")
        val connection = (url.openConnection() as HttpURLConnection).apply { connectTimeout = 15_000; readTimeout = 15_000 }
        val text = try { connection.inputStream.bufferedReader(Charsets.ISO_8859_1).readText() } finally { connection.disconnect() }
        val journeys = JSONObject(text.substring(text.indexOf('{'))).getJSONArray("journey") // the answer is "journeysObj = {...}"
        (0 until minOf(journeys.length(), 10)).joinToString("") { i ->
            val journey = journeys.getJSONObject(i)
            val rt = journey.optJSONObject("rt") // false without real time data
            val time = journey.getString("ti")
            val cancelled = rt?.optString("status") == "Ausfall"
            val expected = if (rt == null || rt.isNull("dlt")) time else rt.getString("dlt")
            val delayed = !cancelled && expected != time
            val product = journey.getString("pr")
            val type = if (product.startsWith("S")) "S" else if (product.startsWith("REX")) "X" else product.take(1)
            expected.replace(":", "") + type + (if (cancelled) "C" else if (delayed) "D" else "0")
        }
    } catch (e: Exception) {
        status = "departures: $e"
        null
    }

    // true if it reached the watchface
    private suspend fun send(): Boolean {
        val note = if (mode == 2 && wasInVienna) "Rad: ${bikeAt(this) ?: "?"}" else ""
        val sender = DefaultPebbleSender(applicationContext)
        try {
            val result = sender.sendDataToPebble(WATCHFACE_UUID, mapOf(KEY_TRAIN_DEPARTURES to PebbleDictionaryItem.Text(departures), KEY_TRAIN_NOTE to PebbleDictionaryItem.Text(note)))
            lastSend = "${LocalTime.now().withNano(0)} \"$note\" $departures: " + (result?.values?.joinToString { it.javaClass.simpleName } ?: "no Pebble app found")
            return !result.isNullOrEmpty() && result.values.all { it == TransmissionResult.Success }
        } finally {
            try { sender.close() } catch (e: IllegalArgumentException) {} // "Service not registered": cancelled (by the next update) before PebbleKit was bound to the Pebble app
        }
    }

    // sends them again, e.g. the watchface was opened
    fun resend() {
        scope.launch { send() }
    }

    // the morning / evening is over: clears the watch and stops; done = for the rest of it (on the train / home), else its time is over
    private fun finish(done: Boolean) {
        if (done) getSharedPreferences(PREFS, MODE_PRIVATE).edit { putString(PREF_TRAIN_DONE, "${LocalDate.now()}/$mode") }
        status = "${if (done) "done" else "over"} at ${LocalTime.now().withNano(0)}"
        mode = 0
        departures = ""
        fetching?.cancel()
        LocationManagerCompat.removeUpdates(getSystemService(LocationManager::class.java), listener)
        requestTransitions(false)
        scheduleTrains(this, false)
        scope.launch {
            // the watch keeps showing the departures until they are cleared: again if it did not get it (e.g. another app open on the watch)
            try { repeat(20) { if (send()) return@launch; delay(30_000) } } finally { stopSelf() }
        }
    }

    @SuppressLint("MissingPermission") // startForeground fails without it
    private fun requestLocation(precise: Boolean) {
        if (precise == this.precise) return
        this.precise = precise
        val manager = getSystemService(LocationManager::class.java)
        LocationManagerCompat.removeUpdates(manager, listener)
        // fused also for the GPS (high accuracy): the GPS alone has no positions in the train, and without positions it would never switch back nor notice leaving the area
        val provider = if (Build.VERSION.SDK_INT >= 31 && manager.hasProvider(LocationManager.FUSED_PROVIDER)) LocationManager.FUSED_PROVIDER else if (precise) LocationManager.GPS_PROVIDER else LocationManager.NETWORK_PROVIDER
        val request = LocationRequestCompat.Builder(if (precise) 5_000L else 60_000L).setQuality(if (precise) LocationRequestCompat.QUALITY_HIGH_ACCURACY else LocationRequestCompat.QUALITY_BALANCED_POWER_ACCURACY).build()
        try {
            LocationManagerCompat.requestLocationUpdates(manager, provider, request, ContextCompat.getMainExecutor(this), listener)
        } catch (e: Exception) {
            status = "location: $e"
        }
    }

    // the bike ride ending (ON_BICYCLE exit), starting again (enter) and riding in a vehicle (IN_VEHICLE enter, the train), in the morning
    @SuppressLint("MissingPermission") // the failure is shown in status
    private fun requestTransitions(on: Boolean) {
        val intent = PendingIntent.getBroadcast(this, 2, Intent(this, TransitionReceiver::class.java), PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT) // Google Play services adds the result
        val client = ActivityRecognition.getClient(this)
        try {
            if (!on) client.removeActivityTransitionUpdates(intent)
            else client.requestActivityTransitionUpdates(ActivityTransitionRequest(listOf(
                ActivityTransition.Builder().setActivityType(DetectedActivity.ON_BICYCLE).setActivityTransition(ActivityTransition.ACTIVITY_TRANSITION_ENTER).build(),
                ActivityTransition.Builder().setActivityType(DetectedActivity.ON_BICYCLE).setActivityTransition(ActivityTransition.ACTIVITY_TRANSITION_EXIT).build(),
                ActivityTransition.Builder().setActivityType(DetectedActivity.IN_VEHICLE).setActivityTransition(ActivityTransition.ACTIVITY_TRANSITION_ENTER).build(),
            )), intent).addOnFailureListener { activities = "activity recognition failed: $it" }
        } catch (e: Exception) { // no permission for the physical activity
            activities = "activity recognition failed: $e"
        }
    }

    fun onTransitions(events: List<ActivityTransitionEvent>) {
        if (mode != 1) return
        for (event in events.sortedBy { it.elapsedRealTimeNanos }) {
            val t = event.elapsedRealTimeNanos / 1_000_000
            val station = stationAt(t)
            val bike = event.activityType == DetectedActivity.ON_BICYCLE
            val enter = event.transitionType == ActivityTransition.ACTIVITY_TRANSITION_ENTER
            activities += "${LocalTime.now().withNano(0)} ${if (bike) "bike" else "vehicle"} ${if (enter) "start" else "end"} at ${station?.name ?: "?"}\n"
            if (bike) rode = true
            if (bike && enter) bikeParked = false
            else if (bike) { // the ride ended
                bikeParked = station != null
                if (station != null) saveBike(station.name)
            } else { // on the train
                if (!bikeParked) saveBike(if (rode) station?.name else BIKE_HOME)
                return finish(true)
            }
        }
    }

    private fun saveBike(at: String?) = getSharedPreferences(PREFS, MODE_PRIVATE).edit { putString(PREF_BIKE, at); putString(PREF_BIKE_DATE, LocalDate.now().toString()) }

    // the station within BIKE_RADIUS of where the phone was at t (ms of elapsedRealtime): the last position before it that was not faster than walking
    // (the activity recognition can be late, e.g. already on the train), null if none
    private fun stationAt(t: Long): Station? {
        val fix = track.lastOrNull { it.first.elapsedRealtimeNanos / 1_000_000 <= t && (it.second ?: 0f) < WALKING_KMH }?.first ?: return null
        if (t - fix.elapsedRealtimeNanos / 1_000_000 > TRACK_MAX_AGE_MS) return null
        return HOME_STATIONS.minBy { it.distance(fix) }.takeIf { it.distance(fix) < BIKE_RADIUS }
    }

    private fun onLocation(fix: Location) {
        val prev = last
        last = fix
        // km/h from the GPS or between two accurate positions, null if unknown
        val kmh = when {
            fix.hasSpeed() && fix.accuracy < ACCURATE -> fix.speed * 3.6f
            prev != null && fix.accuracy < ACCURATE && prev.accuracy < ACCURATE && fix.elapsedRealtimeNanos - prev.elapsedRealtimeNanos > 3_000_000_000 -> prev.distanceTo(fix) / ((fix.elapsedRealtimeNanos - prev.elapsedRealtimeNanos) / 1e9f) * 3.6f
            else -> null
        }
        val now = fix.elapsedRealtimeNanos / 1_000_000
        if (mode == 1) {
            if (first == null && fix.accuracy < 200) first = fix
            if (fix.accuracy < TRACK_ACCURACY) track += fix to kmh
            if (track.size > 2000) track.removeAt(0)
            status = "morning, ${LocalTime.now().withNano(0)}: ${fix.provider} ±${fix.accuracy.toInt()} m, ${kmh?.toInt() ?: "?"} km/h, bike ${bikeAt(this) ?: "?"}"
            if (PRESSBAUM.distance(fix) > AREA_RADIUS) { // on the train, the activity recognition missed it
                if (!bikeParked) saveBike(if (rode) stationAt(now)?.name else BIKE_HOME)
                return finish(true)
            }
            requestLocation(HOME_STATIONS.any { it.distance(fix) < NEAR_RADIUS } && (first?.let { it.distanceTo(fix) > LEFT_HOME } ?: false))
        } else if (mode == 2) {
            val vienna = WESTBAHNHOF.distance(fix) < VIENNA_RADIUS
            if (vienna != inVienna) {
                inVienna = vienna
                wasInVienna = wasInVienna || vienna
                update()
            }
            // home: in the area of Pressbaum for HOME_MS (also if not in Vienna today)
            homeSince = if (PRESSBAUM.distance(fix) < AREA_RADIUS) homeSince ?: now else null
            status = "evening, ${LocalTime.now().withNano(0)}: ${fix.provider} ±${fix.accuracy.toInt()} m, ${if (inVienna) "in Vienna" else "not in Vienna"}" +
                "${homeSince?.let { ", home for ${(now - it) / 60_000} min" } ?: ""}, bike ${bikeAt(this) ?: "?"}"
            if (homeSince?.let { now - it >= HOME_MS } == true) return finish(true)
        }
    }

    companion object {
        var instance: TrainService? = null
        var status = "not started yet"
        var lastSend = "nothing sent yet"
        var activities = ""
    }
}
