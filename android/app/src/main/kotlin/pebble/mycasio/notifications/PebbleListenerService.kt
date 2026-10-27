package pebble.mycasio.notifications

import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.ReceiveResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import java.util.UUID

// The Pebble app binds this service when the watchface is opened; it does not know the icons and the trains yet then.
class PebbleListenerService : BasePebbleListenerService() {
    override fun onAppOpened(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID != WATCHFACE_UUID) return
        NotificationIconService.instance?.update(force = true)
        TrainService.instance?.resend()
    }

    override suspend fun onMessageReceived(watchappUUID: UUID, data: PebbleDictionary, watch: WatchIdentifier): ReceiveResult = ReceiveResult.Ack
}
