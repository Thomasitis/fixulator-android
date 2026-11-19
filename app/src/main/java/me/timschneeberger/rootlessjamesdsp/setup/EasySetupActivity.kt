package me.timschneeberger.rootlessjamesdsp.setup

import android.Manifest
import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.content.ContextCompat
import me.timschneeberger.rootlessjamesdsp.R
import me.timschneeberger.rootlessjamesdsp.activity.MainActivity
import me.timschneeberger.rootlessjamesdsp.utils.extensions.PermissionExtensions.hasDumpPermission
import me.timschneeberger.rootlessjamesdsp.utils.extensions.PermissionExtensions.hasProjectMediaAppOp

/**
 * Fixulator easy setup (Android 11+): developer mode -> Wireless debugging -> pairing code in a notification.
 * Fixulator then grants itself the permissions it needs. No Shizuku app, no computer.
 */
class EasySetupActivity : Activity() {
    private lateinit var step1: TextView
    private lateinit var step2: TextView
    private lateinit var step3: TextView
    private lateinit var btn1: Button
    private lateinit var btn2: Button
    private lateinit var btn3: Button
    private lateinit var status: TextView

    private val amber = Color.parseColor("#F2A93B")
    private val bg = Color.parseColor("#17181B")
    private val card = Color.parseColor("#22252A")
    private val dim = Color.parseColor("#9AA0A6")

    private val finished = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) { refresh() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(28), dp(22), dp(28))
            setBackgroundColor(bg)
        }
        root.addView(text("FIXULATOR", 26f, Color.WHITE, bold = true))
        root.addView(text(getString(R.string.fx_setup_intro), 15f, dim).apply { setPadding(0, dp(8), 0, dp(18)) })

        step1 = stepTitle(); btn1 = button(getString(R.string.fx_setup_btn1)) { openAboutPhone() }
        step2 = stepTitle(); btn2 = button(getString(R.string.fx_setup_btn2)) { openWirelessDebugging() }
        step3 = stepTitle(); btn3 = button(getString(R.string.fx_setup_btn3)) { startPairing() }
        root.addView(stepCard(step1, getString(R.string.fx_setup_step1_text), btn1))
        root.addView(stepCard(step2, getString(R.string.fx_setup_step2_text), btn2))
        root.addView(stepCard(step3, getString(R.string.fx_setup_step3_text), btn3))

        status = text("", 15f, amber, bold = true).apply { setPadding(0, dp(14), 0, dp(14)) }
        root.addView(status)

        root.addView(text(getString(R.string.fx_setup_other_method), 13f, dim).apply {
            setPadding(0, dp(18), 0, 0)
            setOnClickListener { useOtherMethod() }
        })

        setContentView(ScrollView(this).apply { setBackgroundColor(bg); addView(root) })
    }

    override fun onResume() {
        super.onResume()
        ContextCompat.registerReceiver(this, finished, IntentFilter(PairingService.ACTION_FINISHED), ContextCompat.RECEIVER_NOT_EXPORTED)
        refresh()
    }

    override fun onPause() {
        runCatching { unregisterReceiver(finished) }
        super.onPause()
    }

    private fun devOptionsOn() = Settings.Global.getInt(contentResolver, Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, 0) == 1
    private fun wirelessDebuggingOn() = Settings.Global.getInt(contentResolver, "adb_wifi_enabled", 0) == 1
    private fun permissionsDone() = hasDumpPermission() && hasProjectMediaAppOp()

    private fun refresh() {
        if (permissionsDone()) {
            status.text = getString(R.string.fx_setup_all_done)
            startActivity(Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
            finish()
            return
        }
        val d = devOptionsOn(); val w = wirelessDebuggingOn()
        step1.text = (if (d) "✓  " else "1  ") + getString(R.string.fx_setup_step1)
        step2.text = (if (w) "✓  " else "2  ") + getString(R.string.fx_setup_step2)
        step3.text = "3  " + getString(R.string.fx_setup_step3)
        btn1.isEnabled = !d
        btn2.isEnabled = d
        btn3.isEnabled = d && w
        for (b in listOf(btn1, btn2, btn3)) b.alpha = if (b.isEnabled) 1f else 0.35f
        step1.setTextColor(if (d) amber else Color.WHITE)
        step2.setTextColor(if (w) amber else Color.WHITE)
        status.text = ""
    }

    private fun openAboutPhone() =
        runCatching { startActivity(Intent(Settings.ACTION_DEVICE_INFO_SETTINGS)) }

    private fun openWirelessDebugging() {
        // Deep link to Wireless debugging where the phone supports it, otherwise Developer options
        val wireless = Intent("android.settings.WIRELESS_DEBUGGING_SETTINGS")
        if (runCatching { startActivity(wireless) }.isFailure)
            runCatching { startActivity(Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS)) }
    }

    private fun startPairing() {
        ContextCompat.startForegroundService(this, Intent(this, PairingService::class.java))
        status.text = getString(R.string.fx_setup_now_pair)
        openWirelessDebugging()
    }

    private fun useOtherMethod() {
        getSharedPreferences("fixulator", MODE_PRIVATE).edit().putBoolean("easySetupSkipped", true).apply()
        startActivity(Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        finish()
    }

    // ---- tiny UI helpers
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
    private fun text(s: String, size: Float, color: Int, bold: Boolean = false) = TextView(this).apply {
        text = s; textSize = size; setTextColor(color); if (bold) typeface = Typeface.DEFAULT_BOLD
    }
    private fun stepTitle() = text("", 17f, Color.WHITE, bold = true)
    private fun button(label: String, onClick: () -> Unit) = Button(this).apply {
        text = label; isAllCaps = false; setTextColor(Color.BLACK)
        background = GradientDrawable().apply { cornerRadius = dp(12).toFloat(); setColor(amber) }
        setOnClickListener { onClick() }
    }
    private fun stepCard(title: TextView, body: String, btn: Button): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(16), dp(14), dp(16), dp(16))
        background = GradientDrawable().apply { cornerRadius = dp(16).toFloat(); setColor(card) }
        addView(title)
        addView(text(body, 14f, dim).apply { setPadding(0, dp(6), 0, dp(12)) })
        addView(btn, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(48)))
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
            .apply { bottomMargin = dp(12) }
        gravity = Gravity.START
    }
}
