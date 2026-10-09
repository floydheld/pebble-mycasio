package pebble.mycasio.notifications

import android.app.Notification
import android.app.NotificationManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.os.BatteryManager
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import io.rebble.pebblekit2.client.DefaultPebbleSender
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.TransmissionResult
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.UUID

val WATCHFACE_UUID: UUID = UUID.fromString("03adc57a-569b-4669-9a80-b505eaea314d") // uuid in the watchface's package.json
const val KEY_NOTIF_ICONS = 130u // messageKeys in the watchface's package.json
const val KEY_PHONE_BATTERY = 131u
const val ICON_SIZE = 27         // NOTIF_ICON_SIZE in the watchface
const val ICON_BYTES = (ICON_SIZE * ICON_SIZE + 7) / 8
const val MAX_ICONS = 6

// Sends the icons of the notifications, as they are shown in the status bar of the phone, to the watchface:
// count, then per icon ICON_SIZE x ICON_SIZE bits row by row (most significant bit first), 1 = opaque; and the battery level of the phone in percent.
class NotificationIconService : NotificationListenerService() {
    private val scope = MainScope()
    private var pending: Job? = null
    private var force = false
    private var lastSent: ByteArray? = null
    private var battery = -1
    private var lastSentBattery = -1

    private val batteryReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) * 100 / intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
            if (level != battery) {
                battery = level
                update()
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        registerReceiver(batteryReceiver, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
    }

    // the media notifications are recognized by the apps with an active media session (see iconData)
    private val sessionsListener = MediaSessionManager.OnActiveSessionsChangedListener { update() }

    override fun onListenerConnected() {
        instance = this
        getSystemService(MediaSessionManager::class.java).addOnActiveSessionsChangedListener(sessionsListener, ComponentName(this, NotificationIconService::class.java))
        update(force = true)
    }

    override fun onListenerDisconnected() {
        instance = null
        getSystemService(MediaSessionManager::class.java).removeOnActiveSessionsChangedListener(sessionsListener)
    }

    override fun onDestroy() {
        instance = null
        unregisterReceiver(batteryReceiver)
        scope.cancel()
        super.onDestroy()
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) = update()
    override fun onNotificationRemoved(sbn: StatusBarNotification?) = update()
    override fun onNotificationRankingUpdate(rankingMap: RankingMap?) = update()

    // sends the icons a moment after the last change (notifications often come in bursts), only if they changed (or forced)
    fun update(force: Boolean = false) {
        this.force = this.force || force
        pending?.cancel()
        pending = scope.launch {
            delay(500)
            val data = iconData()
            val battery = battery
            if (!this@NotificationIconService.force && data.contentEquals(lastSent) && battery == lastSentBattery) return@launch
            val sender = DefaultPebbleSender(applicationContext)
            try {
                val result = sender.sendDataToPebble(WATCHFACE_UUID, mapOf(KEY_NOTIF_ICONS to PebbleDictionaryItem.Bytes(data), KEY_PHONE_BATTERY to PebbleDictionaryItem.Int32(battery)))
                lastResult = "${(data[0])} icons, battery $battery %: " + (result?.values?.joinToString { it.javaClass.simpleName } ?: "no Pebble app found")
                if (!result.isNullOrEmpty() && result.values.all { it == TransmissionResult.Success }) {
                    lastSent = data
                    lastSentBattery = battery
                    this@NotificationIconService.force = false
                } else {
                    lastSent = null // e.g. another app is open on the watch: send again with the next change or when the watchface is opened
                }
            } finally {
                sender.close()
            }
        }
    }

    override fun onSilentStatusBarIconsVisibilityChanged(hideSilentStatusIcons: Boolean) = update()

    // like the status bar: in the order of the ranking, without the minimized notifications, the silent ones if the phone hides them in the status bar
    // ("Hide silent notifications in status bar"), those hidden by Do Not Disturb and the children of a group with a summary (the status bar shows one icon
    // per group); unlike the status bar also without the media ones (e.g. VLC: media style, or ongoing from an app with a media session); each icon only once; the reasons are listed in lastReport
    private fun iconData(): ByteArray {
        val rankingMap = currentRanking ?: return byteArrayOf(0)
        val ranking = Ranking()
        val byKey = (activeNotifications ?: emptyArray()).associateBy { it.key }
        val icons = mutableListOf<ByteArray>()
        val report = StringBuilder()
        val mediaApps = try { getSystemService(MediaSessionManager::class.java).getActiveSessions(ComponentName(this, NotificationIconService::class.java)).map(MediaController::getPackageName).toSet() } catch (e: SecurityException) { emptySet() }
        val summaries = byKey.values.filter { it.notification.flags and Notification.FLAG_GROUP_SUMMARY != 0 }.map { it.groupKey }.toSet()
        val hideSilent = Build.VERSION.SDK_INT >= 29 && getSystemService(NotificationManager::class.java).shouldHideSilentStatusBarIcons()
        for (key in rankingMap.orderedKeys) {
            val sbn = byKey[key] ?: continue
            if (!rankingMap.getRanking(key, ranking)) continue
            val hidden = when {
                ranking.isAmbient -> "minimized"
                sbn.notification.extras.containsKey(Notification.EXTRA_MEDIA_SESSION) || sbn.notification.extras.getString(Notification.EXTRA_TEMPLATE)?.contains("Media") == true || sbn.notification.category == Notification.CATEGORY_TRANSPORT -> "media"
                sbn.isOngoing && sbn.packageName in mediaApps -> "media (ongoing, the app has a media session)"
                sbn.packageName == "com.android.systemui" && ranking.channel?.id?.contains("Media") == true -> "media (of System UI, e.g. MediaOngoingActivity)"
                sbn.packageName == "com.android.systemui" && ranking.channel?.id == "ZEN_ONGOING" -> "Do Not Disturb (the status bar shows it as a status icon)"
                sbn.notification.flags and Notification.FLAG_GROUP_SUMMARY == 0 && sbn.groupKey in summaries -> "in a group (the status bar shows the icon of its summary)"
                hideSilent && ranking.importance < NotificationManager.IMPORTANCE_DEFAULT -> "silent (hidden in the status bar)"
                Build.VERSION.SDK_INT >= 28 && (ranking.suppressedVisualEffects and NotificationManager.Policy.SUPPRESSED_EFFECT_STATUS_BAR) != 0 -> "hidden by Do Not Disturb"
                icons.size == MAX_ICONS -> "too many"
                else -> null
            }
            val icon = if (hidden == null) renderIcon(sbn) else null
            val state = when {
                hidden != null -> hidden
                icon == null -> "no icon"
                icons.any { it.contentEquals(icon) } -> "same icon as before"
                else -> { icons += icon; "SENT" }
            }
            report.append("${sbn.packageName} (${ranking.channel?.id}, importance ${ranking.importance}${if (sbn.isOngoing) ", ongoing" else ""}): $state\n")
        }
        lastReport = report.toString()
        return byteArrayOf(icons.size.toByte()) + icons.fold(ByteArray(0)) { all, icon -> all + icon }
    }

    // the small icon (a silhouette, which the status bar tints) as 1 bit per pixel from its alpha;
    // without its empty margin and scaled to fill the ICON_SIZE square
    private fun renderIcon(sbn: StatusBarNotification): ByteArray? {
        val drawable = try { sbn.notification.smallIcon?.loadDrawable(this) } catch (e: Exception) { null } ?: return null
        val big = Bitmap.createBitmap(4 * ICON_SIZE, 4 * ICON_SIZE, Bitmap.Config.ARGB_8888)
        drawable.setBounds(0, 0, big.width, big.height)
        drawable.draw(Canvas(big))
        val content = Rect(big.width, big.height, 0, 0)
        for (y in 0 until big.height) for (x in 0 until big.width) if (Color.alpha(big.getPixel(x, y)) >= 128) {
            content.left = minOf(content.left, x)
            content.top = minOf(content.top, y)
            content.right = maxOf(content.right, x + 1)
            content.bottom = maxOf(content.bottom, y + 1)
        }
        if (content.isEmpty) return null
        val scale = ICON_SIZE.toFloat() / maxOf(content.width(), content.height())
        val w = content.width() * scale
        val h = content.height() * scale
        val bitmap = Bitmap.createBitmap(ICON_SIZE, ICON_SIZE, Bitmap.Config.ARGB_8888)
        Canvas(bitmap).drawBitmap(big, content, RectF((ICON_SIZE - w) / 2, (ICON_SIZE - h) / 2, (ICON_SIZE + w) / 2, (ICON_SIZE + h) / 2), Paint(Paint.FILTER_BITMAP_FLAG))
        big.recycle()
        val bits = ByteArray(ICON_BYTES)
        for (y in 0 until ICON_SIZE) for (x in 0 until ICON_SIZE) {
            val i = y * ICON_SIZE + x
            if (Color.alpha(bitmap.getPixel(x, y)) >= 128) bits[i / 8] = (bits[i / 8].toInt() or (0x80 shr (i % 8))).toByte()
        }
        bitmap.recycle()
        return bits
    }

    companion object {
        var instance: NotificationIconService? = null
        var lastResult = "nothing sent yet"
        var lastReport = ""
    }
}
