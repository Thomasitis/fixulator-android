package me.timschneeberger.rootlessjamesdsp.widget

import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import me.timschneeberger.rootlessjamesdsp.utils.EngineUtils.toggleEnginePower

/** Invisible activity behind the widget: switches Fixulator on or off (an activity may ask Android for capture permission). */
class WidgetToggleActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val turnOn = !FixulatorWidget.isOn()
        val app = applicationContext
        toggleEnginePower(turnOn) { startActivity(it) }
        Handler(Looper.getMainLooper()).postDelayed({ FixulatorWidget.updateAll(app) }, 1500)
        finish()
    }
}
