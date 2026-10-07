package pebble.mycasio.notifications

import android.app.Notification
import android.app.NotificationManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
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
const val ICON_SIZE = 27         // NOTIF_ICON_SIZE in the watchface
const val ICON_BYTES = (ICON_SIZE * ICON_SIZE + 7) / 8
const val MAX_ICONS = 6

// Sends the icons of the notifications, as they are shown in the status bar of the phone, to the watchface:
// count, then per icon ICON_SIZE x ICON_SIZE bits row by row (most significant bit first), 1 = opaque.
class NotificationIconService : NotificationListenerService() {
    private val scope = MainScope()
    private var pending: Job? = null
    private var force = false
    private var lastSent: ByteArray? = null

    override fun onListenerConnected() {
        instance = this
        update(force = true)
    }

    override fun onListenerDisconnected() {
        instance = null
    }

    override fun onDestroy() {
        instance = null
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
            if (!this@NotificationIconService.force && data.contentEquals(lastSent)) return@launch
            val sender = DefaultPebbleSender(applicationContext)
            try {
                val result = sender.sendDataToPebble(WATCHFACE_UUID, mapOf(KEY_NOTIF_ICONS to PebbleDictionaryItem.Bytes(data)))
                lastResult = "${(data[0])} icons: " + (result?.values?.joinToString { it.javaClass.simpleName } ?: "no Pebble app found")
                if (!result.isNullOrEmpty() && result.values.all { it == TransmissionResult.Success }) {
                    lastSent = data
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
    // ("Hide silent notifications in status bar"), those hidden by Do Not Disturb, the media ones (Android 11+ shows them only in the media player of the
    // quick settings, e.g. VLC) and the children of a group with a summary (the status bar shows one icon per group), each icon only once; the reasons are listed in lastReport
    private fun iconData(): ByteArray {
        val rankingMap = currentRanking ?: return byteArrayOf(0)
        val ranking = Ranking()
        val byKey = (activeNotifications ?: emptyArray()).associateBy { it.key }
        val icons = mutableListOf<ByteArray>()
        val report = StringBuilder()
        val summaries = byKey.values.filter { it.notification.flags and Notification.FLAG_GROUP_SUMMARY != 0 }.map { it.groupKey }.toSet()
        val hideSilent = Build.VERSION.SDK_INT >= 29 && getSystemService(NotificationManager::class.java).shouldHideSilentStatusBarIcons()
        for (key in rankingMap.orderedKeys) {
            val sbn = byKey[key] ?: continue
            if (!rankingMap.getRanking(key, ranking)) continue
            val hidden = when {
                ranking.isAmbient -> "minimized"
                Build.VERSION.SDK_INT >= 30 && sbn.notification.extras.containsKey(Notification.EXTRA_MEDIA_SESSION) -> "media (the phone shows it in the media player instead)"
                sbn.notification.flags and Notification.FLAG_GROUP_SUMMARY == 0 && sbn.groupKey in summaries -> "in a group (the status bar shows the icon of its summary)"
                sbn.packageName == "com.android.systemui" && ranking.channel?.id == "ZEN_ONGOING" -> "Do Not Disturb (the status bar shows it as a status icon)"
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
