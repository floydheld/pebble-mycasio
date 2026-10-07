package pebble.mycasio.notifications

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.app.NotificationManagerCompat
import io.rebble.pebblekit2.client.DefaultPebbleAndroidAppPicker

// Status and the two things to do: allow the notification access, send the icons now.
class MainActivity : Activity() {
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        status = TextView(this).apply { textSize = 16f }
        val access = Button(this).apply {
            text = "Notification access"
            setOnClickListener { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
        }
        val send = Button(this).apply {
            text = "Send icons now"
            setOnClickListener {
                NotificationIconService.instance?.update(force = true)
                status.postDelayed({ showStatus() }, 2000)
            }
        }
        setContentView(ScrollView(this).apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(48, 96, 48, 48)
                addView(access)
                addView(send)
                addView(status)
            })
        })
    }

    override fun onResume() {
        super.onResume()
        showStatus()
    }

    private fun showStatus() {
        val enabled = NotificationManagerCompat.getEnabledListenerPackages(this).contains(packageName)
        val pebbleApps = DefaultPebbleAndroidAppPicker.getInstance(this).getAllEligibleApps()
        status.text = "Notification access: ${if (enabled) "allowed" else "NOT allowed"}\n" +
            "Listener running: ${NotificationIconService.instance != null}\n" +
            "Pebble app: ${pebbleApps.firstOrNull() ?: "not found"}\n" +
            "Last send: ${NotificationIconService.lastResult}\n\n" +
            "Notifications:\n${NotificationIconService.lastReport}"
    }
}
