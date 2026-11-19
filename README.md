# Fixulator for Android

System-wide spoken-voice balance for podcasts: Fixulator sits at the very end of the phone's audio,
after YouTube, Spotify or any podcast player, and balances voices toward a reference recipe
(balance EQ, plosive guard, de-esser, leveller, limiter).

Download the newest build from the **Releases → latest** page and install `Fixulator.apk`.

## Credits and licence
Fixulator for Android is built on [RootlessJamesDSP](https://github.com/timschneeb/RootlessJamesDSP)
by Tim Schneeberger and [libjamesdsp](https://github.com/ThePBone/JamesDSPManager) by James Fung and contributors,
which handle the system-wide capture and playback. Like them, this app is licensed under the
GNU General Public License v3 (see `LICENSE`). The Fixulator engine lives in `app/src/main/cpp/fixulator`.

## Builds
Every push to `main` builds a signed `Fixulator.apk` (same key every time, so updates install over the old version)
and publishes it under **Releases → latest**.
