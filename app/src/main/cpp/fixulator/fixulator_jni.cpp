// Fixulator engine for Android: JNI bridge used by the capture service (interleaved stereo float in, in place).
#include <jni.h>
#include <vector>
#include <memory>
#include "FixDsp.h"

namespace
{
fx::Recipe referenceRecipe()
{
    fx::Recipe r; r.name = "Reference podcast"; r.source = "En million til bibliotek i Namibia.mp3"; r.valid = true;
    const float p[fx::kNumBands] = { -13.35f, -6.01f, 6.33f, 6.88f, 7.49f, 10.34f, 7.26f, 8.59f, 9.81f, 10.38f, 9.18f, 3.49f,
                                     0.74f, 1.10f, 0.87f, -2.38f, -2.92f, -2.29f, -6.25f, -7.48f, -7.86f, -8.37f, -10.42f, -15.15f };
    for (int b = 0; b < fx::kNumBands; ++b) r.profile[(size_t) b] = p[b];
    r.sibPeak = -2.99f;
    return r;
}

struct Fixulator
{
    fx::Engine engine;
    fx::Params params;
    std::vector<float> L, R;
    double sr = 0;
    bool enabled = true;
    Fixulator()
    {
        params.fix = 100; params.deess = 50; params.plosive = 50; params.levelTarget = -18;
        params.slowMode = true;   // listening: keep adapting gently to whoever is talking
        engine.setRecipe (referenceRecipe());
    }
    void ensure (double rate)
    {
        if (rate != sr) { sr = rate; engine.prepare (sr, 4096); }
    }
};
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_me_timschneeberger_rootlessjamesdsp_interop_FixulatorEngine_nativeCreate (JNIEnv*, jclass)
{
    return (jlong) new Fixulator();
}

JNIEXPORT void JNICALL
Java_me_timschneeberger_rootlessjamesdsp_interop_FixulatorEngine_nativeDestroy (JNIEnv*, jclass, jlong h)
{
    delete (Fixulator*) h;
}

JNIEXPORT void JNICALL
Java_me_timschneeberger_rootlessjamesdsp_interop_FixulatorEngine_nativeSetParams (JNIEnv*, jclass, jlong h, jboolean enabled,
                                                                                   jfloat fix, jfloat deess, jfloat plosive, jfloat levelTarget, jboolean levelOn)
{
    auto* f = (Fixulator*) h; if (! f) return;
    f->enabled = enabled; f->params.fix = fix; f->params.deess = deess; f->params.plosive = plosive;
    f->params.levelTarget = levelTarget; f->params.levelOn = levelOn;
}

JNIEXPORT jint JNICALL
Java_me_timschneeberger_rootlessjamesdsp_interop_FixulatorEngine_nativeLatency (JNIEnv*, jclass, jlong h, jint rate)
{
    auto* f = (Fixulator*) h; if (! f) return 0;
    f->ensure (rate); return f->engine.getLatency();
}

JNIEXPORT void JNICALL
Java_me_timschneeberger_rootlessjamesdsp_interop_FixulatorEngine_nativeProcess (JNIEnv* env, jclass, jlong h, jfloatArray buf, jint length, jint rate)
{
    auto* f = (Fixulator*) h; if (! f || ! f->enabled) return;
    f->ensure (rate);
    const int frames = length / 2;
    if (frames <= 0) return;
    if ((int) f->L.size() < frames) { f->L.resize ((size_t) frames); f->R.resize ((size_t) frames); }
    jfloat* d = env->GetFloatArrayElements (buf, nullptr);
    for (int i = 0; i < frames; ++i) { f->L[(size_t) i] = d[2 * i]; f->R[(size_t) i] = d[2 * i + 1]; }
    float* io[2] = { f->L.data(), f->R.data() };
    f->engine.process (io, 2, frames, f->params);
    for (int i = 0; i < frames; ++i) { d[2 * i] = f->L[(size_t) i]; d[2 * i + 1] = f->R[(size_t) i]; }
    env->ReleaseFloatArrayElements (buf, d, 0);
}

JNIEXPORT jfloatArray JNICALL
Java_me_timschneeberger_rootlessjamesdsp_interop_FixulatorEngine_nativeStatus (JNIEnv* env, jclass, jlong h)
{
    // [learned %, speaker pitch Hz, in LUFS, out LUFS, worst tone light 0/1/2]
    auto* f = (Fixulator*) h;
    float v[5] = { 0, 0, -70, -70, 0 };
    if (f)
    {
        auto& m = f->engine.meters;
        int worst = 0; for (int t = 0; t < fx::NumTones; ++t) worst = std::max (worst, (int) m.tone[t]);
        v[0] = m.learnProgress.load() * 100; v[1] = f->engine.getSpeakerF0(); v[2] = m.inLufs.load(); v[3] = m.outLufs.load(); v[4] = (float) worst;
    }
    jfloatArray out = env->NewFloatArray (5);
    env->SetFloatArrayRegion (out, 0, 5, v);
    return out;
}

}
