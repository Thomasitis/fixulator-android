package me.timschneeberger.rootlessjamesdsp.widget

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.widget.RemoteViews
import me.timschneeberger.rootlessjamesdsp.R
import me.timschneeberger.rootlessjamesdsp.service.BaseAudioProcessorService

/** Home-screen on/off button for Fixulator. */
class FixulatorWidget : AppWidgetProvider() {

    override fun onUpdate(context: Context, manager: AppWidgetManager, ids: IntArray) {
        ids.forEach { manager.updateAppWidget(it, buildViews(context)) }
    }

    companion object {
        fun isOn(): Boolean = BaseAudioProcessorService.activeServices > 0

        private fun buildViews(context: Context): RemoteViews {
            val on = isOn()
            val views = RemoteViews(context.packageName, R.layout.widget_fixulator)
            views.setTextViewText(R.id.fx_widget_state, if (on) "ON" else "OFF")
            views.setTextViewText(R.id.fx_widget_hint, if (on) "tap to switch off" else "tap to switch on")
            views.setInt(R.id.fx_widget_root, "setBackgroundResource",
                if (on) R.drawable.fx_widget_bg_on else R.drawable.fx_widget_bg_off)
            val tap = Intent(context, WidgetToggleActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_ANIMATION)
            views.setOnClickPendingIntent(R.id.fx_widget_root,
                PendingIntent.getActivity(context, 0, tap, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT))
            return views
        }

        /** Refresh every placed widget (called when the service starts or stops). */
        fun updateAll(context: Context) {
            val manager = AppWidgetManager.getInstance(context) ?: return
            val ids = manager.getAppWidgetIds(ComponentName(context, FixulatorWidget::class.java))
            if (ids.isNotEmpty()) ids.forEach { manager.updateAppWidget(it, buildViews(context)) }
        }
    }
}
