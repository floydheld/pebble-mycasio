package pebble.mycasio.notifications

import android.Manifest
import android.app.Activity
import android.app.ActivityManager
import android.app.Application
import android.app.ApplicationExitInfo
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.edit
import androidx.core.widget.doAfterTextChanged
import io.rebble.pebblekit2.client.DefaultPebbleAndroidAppPicker
import java.text.DateFormat
import java.util.Date

const val PREF_CRASH = "crash" // the stack trace of the last crash (see App)

// Status and the things to do: allow the notification access and the location (trains), send the icons now, set the delay of the notifications.
class MainActivity : Activity() {
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        status = TextView(this).apply { textSize = 16f }
        val access = Button(this).apply {
            text = "Notification access"
            setOnClickListener { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
        }
        // the location while using the app and the physical activity, then (Android 10+, a second dialog or the settings) the location all the time
        val location = Button(this).apply {
            text = "Location and activity access (trains)"
            setOnClickListener {
                val missing = (listOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION) + if (Build.VERSION.SDK_INT >= 29) listOf(Manifest.permission.ACTIVITY_RECOGNITION) else emptyList()).filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
                if (missing.isNotEmpty()) requestPermissions(missing.toTypedArray(), 1)
                else if (Build.VERSION.SDK_INT >= 29) requestPermissions(arrayOf(Manifest.permission.ACCESS_BACKGROUND_LOCATION), 2)
            }
        }
        val send = Button(this).apply {
            text = "Send icons now"
            setOnClickListener {
                NotificationIconService.instance?.update(force = true)
                status.postDelayed({ showStatus() }, 2000)
            }
        }
        val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
        val delayLabel = TextView(this).apply {
            text = "Show a notification on the watch after (minutes, 0 = at once):"
            setPadding(0, 32, 0, 0)
        }
        val delay = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_NUMBER
            setText(prefs.getInt(PREF_DELAY, DEFAULT_DELAY).toString())
            doAfterTextChanged { text ->
                val minutes = text.toString().toIntOrNull() ?: return@doAfterTextChanged
                prefs.edit { putInt(PREF_DELAY, minutes) }
                NotificationIconService.instance?.update()
            }
        }
        setContentView(ScrollView(this).apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(48, 96, 48, 48)
                addView(access)
                addView(location)
                addView(send)
                addView(delayLabel)
                addView(delay)
                addView(status)
            })
        })
    }

    // starts the trains if they are due now
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        sendBroadcast(Intent(this, TrainReceiver::class.java))
        status.postDelayed({ showStatus() }, 2000)
    }

    override fun onResume() {
        super.onResume()
        showStatus()
    }

    private fun showStatus() {
        val enabled = NotificationManagerCompat.getEnabledListenerPackages(this).contains(packageName)
        val pebbleApps = DefaultPebbleAndroidAppPicker.getInstance(this).getAllEligibleApps()
        val granted = { permission: String -> checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED }
        // why Android stopped the app recently: a crash, not responding (ANR, with the main thread from its trace), too many resources...
        val exits = if (Build.VERSION.SDK_INT < 30) "unknown" else getSystemService(ActivityManager::class.java).getHistoricalProcessExitReasons(packageName, 0, 5).joinToString("\n") { exit ->
            val reason = when (exit.reason) {
                ApplicationExitInfo.REASON_CRASH -> "CRASH"
                ApplicationExitInfo.REASON_CRASH_NATIVE -> "NATIVE CRASH"
                ApplicationExitInfo.REASON_ANR -> "NOT RESPONDING"
                ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "TOO MANY RESOURCES"
                ApplicationExitInfo.REASON_LOW_MEMORY -> "low memory"
                ApplicationExitInfo.REASON_USER_REQUESTED -> "stopped by the user"
                ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "permission changed"
                else -> "reason ${exit.reason}"
            }
            val trace = if (exit.reason != ApplicationExitInfo.REASON_ANR) "" else try { exit.traceInputStream?.bufferedReader()?.use { it.readText() }?.substringAfter("\"main\"", "")?.lines()?.take(20)?.joinToString("\n", "\n") ?: "" } catch (e: Exception) { "" }
            "${DateFormat.getDateTimeInstance().format(Date(exit.timestamp))}: $reason, ${exit.description}$trace"
        }.ifEmpty { "none" }
        val location = when {
            !granted(Manifest.permission.ACCESS_FINE_LOCATION) -> "NOT allowed"
            Build.VERSION.SDK_INT >= 29 && !granted(Manifest.permission.ACCESS_BACKGROUND_LOCATION) -> "only while using the app (NOT enough)"
            else -> "allowed all the time"
        }
        status.text = "Notification access: ${if (enabled) "allowed" else "NOT allowed"}\n" +
            "Listener running: ${NotificationIconService.instance != null}\n" +
            "Pebble app: ${pebbleApps.firstOrNull() ?: "not found"}\n" +
            "Last send: ${NotificationIconService.lastResult}\n\n" +
            "Location access: $location\n" +
            "Physical activity access: ${if (Build.VERSION.SDK_INT < 29 || granted(Manifest.permission.ACTIVITY_RECOGNITION)) "allowed" else "NOT allowed"}\n" +
            "Trains: ${if (TrainService.instance != null) "running" else "not running"}, ${TrainService.status}\n" +
            "Bike today: ${bikeAt(this) ?: "unknown"}\n" +
            "Last send: ${TrainService.lastSend}\n" +
            "Activities this morning:\n${TrainService.activities}\n" +
            "Notifications:\n${NotificationIconService.lastReport}\n" +
            "Last stops of the app (by Android):\n$exits\n\n" +
            "Last crash:\n${getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_CRASH, null) ?: "none"}"
    }
}

// Keeps the stack trace of a crash (PREF_CRASH) for MainActivity, then crashes as usual
class App : Application() {
    override fun onCreate() {
        super.onCreate()
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, e ->
            getSharedPreferences(PREFS, MODE_PRIVATE).edit(commit = true) { putString(PREF_CRASH, "${DateFormat.getDateTimeInstance().format(Date())} in ${thread.name}: ${e.stackTraceToString().take(3000)}") }
            previous?.uncaughtException(thread, e)
        }
    }
}
