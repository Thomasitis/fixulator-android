package me.timschneeberger.rootlessjamesdsp.interop

/**
 * Fixulator spoken-voice engine (balance to recipe, plosive guard, de-esser, leveller, limiter).
 * Processes interleaved stereo float buffers in place, after JamesDSP.
 */
class FixulatorEngine : AutoCloseable {
    private var handle: Long = nativeCreate()

    fun setParams(enabled: Boolean, fix: Float = 100f, deess: Float = 50f, plosive: Float = 50f,
                  levelTarget: Float = -18f, levelOn: Boolean = true) {
        if (handle != 0L) nativeSetParams(handle, enabled, fix, deess, plosive, levelTarget, levelOn)
    }

    fun processFloat(buffer: FloatArray, sampleRate: Int, length: Int = buffer.size) {
        if (handle != 0L) nativeProcess(handle, buffer, length, sampleRate)
    }

    fun processInt16(buffer: ShortArray, sampleRate: Int) {
        if (handle == 0L) return
        if (scratch.size < buffer.size) scratch = FloatArray(buffer.size)
        for (i in buffer.indices) scratch[i] = buffer[i] / 32768f
        nativeProcess(handle, scratch, buffer.size, sampleRate)
        for (i in buffer.indices) buffer[i] = (scratch[i] * 32767f).coerceIn(-32768f, 32767f).toInt().toShort()
    }

    /** [learned %, speaker pitch Hz, in LUFS, out LUFS, worst tone light 0/1/2] */
    fun status(): FloatArray = if (handle != 0L) nativeStatus(handle) else floatArrayOf(0f, 0f, -70f, -70f, 0f)

    override fun close() {
        if (handle != 0L) { nativeDestroy(handle); handle = 0L }
    }

    private var scratch = FloatArray(0)

    companion object {
        init { System.loadLibrary("fixulator") }
        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeSetParams(handle: Long, enabled: Boolean, fix: Float, deess: Float,
                                                        plosive: Float, levelTarget: Float, levelOn: Boolean)
        @JvmStatic private external fun nativeLatency(handle: Long, sampleRate: Int): Int
        @JvmStatic private external fun nativeProcess(handle: Long, buffer: FloatArray, length: Int, sampleRate: Int)
        @JvmStatic private external fun nativeStatus(handle: Long): FloatArray
    }
}
