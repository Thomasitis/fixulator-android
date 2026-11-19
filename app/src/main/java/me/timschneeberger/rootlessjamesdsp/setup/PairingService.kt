package me.timschneeberger.rootlessjamesdsp.setup

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat
import androidx.core.app.RemoteInput
import io.github.muntashirakon.adb.android.AdbMdns
import me.timschneeberger.rootlessjamesdsp.R
import timber.log.Timber
import java.net.InetAddress
import kotlin.concurrent.thread

/**
 * Easy setup, step 2: waits for the "Pair device with pairing code" dialog in Wireless debugging,
 * shows a notification where the user types the 6-digit code, pairs, connects and grants
 * Fixulator its permissions through the phone's own ADB. No Shizuku, no computer.
 */
class PairingService : Service() {
    private var mdns: AdbMdns? = null
    @Volatile private var pairingPort = -1
    @Volatile private var busy = false
    private val main = Handler(Looper.getMainLooper())

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> { stopSelf(); return START_NOT_STICKY }
            ACTION_CODE -> {
                val code = RemoteInput.getResultsFromIntent(intent)?.getCharSequence(KEY_CODE)?.toString()?.filter { it.isDigit() }
                if (!code.isNullOrEmpty()) pairAndGrant(code)
                return START_NOT_STICKY
            }
        }
        createChannel()
        val n = waitingNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        else
            startForeground(NOTIF_ID, n)
        startDiscovery()
        return START_NOT_STICKY
    }

    private fun startDiscovery() {
        if (mdns != null) return
        mdns = AdbMdns(this, AdbMdns.SERVICE_TYPE_TLS_PAIRING, object : AdbMdns.OnAdbDaemonDiscoveredListener {
            override fun onPortChanged(hostAddress: InetAddress?, port: Int) {
                Timber.i("Pairing service port: $port")
                if (port > 0 && port != pairingPort) {
                    pairingPort = port
                    main.post { notify(codeNotification()) }
                }
            }
        }).also { it.start() }
    }

    private fun pairAndGrant(code: String) {
        if (busy) return
        busy = true
        notify(progressNotification(getString(R.string.fx_setup_pairing)))
        thread(name = "fixulator-pairing") {
            try {
                val adb = FxAdbManager.get(this)
                val port = pairingPort
                if (port <= 0) throw IllegalStateException("No pairing dialog found")
                if (!adb.pair("127.0.0.1", port, code)) throw IllegalStateException("Pairing refused")
                main.post { notify(progressNotification(getString(R.string.fx_setup_granting))) }
                if (!adb.connectTls(this, 15_000)) throw IllegalStateException("Could not connect after pairing")
                val pkg = packageName
                val out = adb.shell(
                    "pm grant $pkg android.permission.DUMP; " +
                    "appops set $pkg PROJECT_MEDIA allow; " +
                    "appops set $pkg SYSTEM_ALERT_WINDOW allow; " +
                    "pm grant $pkg android.permission.POST_NOTIFICATIONS 2>/dev/null; " +
                    "pm grant $pkg android.permission.RECORD_AUDIO; " +
                    "dumpsys deviceidle whitelist +$pkg >/dev/null; " +
                    "echo FX_DONE")
                runCatching { adb.disconnect() }
                Timber.i("Grant output: $out")
                if (!out.contains("FX_DONE")) throw IllegalStateException("Shell did not finish")
                main.post {
                    notify(doneNotification())
                    sendBroadcast(Intent(ACTION_FINISHED).setPackage(packageName))
                    mdns?.stop(); mdns = null
                    stopForeground(STOP_FOREGROUND_DETACH)
                    stopSelf()
                }
            } catch (e: Exception) {
                Timber.e(e)
                main.post { busy = false; notify(codeNotification(error = e.message ?: e.javaClass.simpleName)) }
            }
        }
    }

    override fun onDestroy() {
        mdns?.stop(); mdns = null
        super.onDestroy()
    }

    // ---- notifications
    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(CHANNEL, getString(R.string.fx_setup_channel), NotificationManager.IMPORTANCE_HIGH)
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).createNotificationChannel(ch)
        }
    }

    private fun notify(n: Notification) =
        (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).notify(NOTIF_ID, n)

    private fun base() = NotificationCompat.Builder(this, CHANNEL)
        .setSmallIcon(R.drawable.ic_tune_vertical_variant_24dp)
        .setContentTitle(getString(R.string.fx_setup_title))
        .setOnlyAlertOnce(false)
        .setPriority(NotificationCompat.PRIORITY_HIGH)

    private fun waitingNotification(): Notification = base()
        .setContentText(getString(R.string.fx_setup_waiting))
        .setStyle(NotificationCompat.BigTextStyle().bigText(getString(R.string.fx_setup_waiting)))
        .setOngoing(true)
        .addAction(0, getString(R.string.fx_setup_cancel), PendingIntent.getService(this, 2,
            Intent(this, PairingService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE))
        .build()

    private fun codeNotification(error: String? = null): Notification {
        val input = RemoteInput.Builder(KEY_CODE).setLabel(getString(R.string.fx_setup_code_hint)).build()
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
                    else PendingIntent.FLAG_UPDATE_CURRENT
        val reply = PendingIntent.getService(this, 1, Intent(this, PairingService::class.java).setAction(ACTION_CODE), flags)
        val action = NotificationCompat.Action.Builder(0, getString(R.string.fx_setup_enter_code), reply)
            .addRemoteInput(input).build()
        val text = if (error == null) getString(R.string.fx_setup_found) else getString(R.string.fx_setup_failed, error)
        return base().setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setOngoing(true)
            .addAction(action)
            .build()
    }

    private fun progressNotification(text: String): Notification = base()
        .setContentText(text).setOngoing(true).setProgress(0, 0, true).build()

    private fun doneNotification(): Notification {
        val open = packageManager.getLaunchIntentForPackage(packageName)
        return base().setContentText(getString(R.string.fx_setup_done))
            .setStyle(NotificationCompat.BigTextStyle().bigText(getString(R.string.fx_setup_done)))
            .setAutoCancel(true)
            .setContentIntent(PendingIntent.getActivity(this, 3, open, PendingIntent.FLAG_IMMUTABLE))
            .build()
    }

    companion object {
        const val CHANNEL = "fixulator_setup"
        const val NOTIF_ID = 4711
        const val KEY_CODE = "pairing_code"
        const val ACTION_CODE = "no.gazpacho.fixulator.PAIR_CODE"
        const val ACTION_STOP = "no.gazpacho.fixulator.PAIR_STOP"
        const val ACTION_FINISHED = "no.gazpacho.fixulator.SETUP_FINISHED"
    }
}
