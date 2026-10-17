# Midna's Lament - Adaptive Battle

Mod by **Nebur**. Arrangement by **Wesley Damaschino**.
Requires **Dusklight 2.0.3**. Version **1.0.2**.

Replaces the music during the Midna's Lament story section. Extra combat
instruments fade in over 0.5 seconds and out over 1.5 seconds. The base arrangement
keeps playing. Other music selections use the game's original music.

## Install

Download the combined `midna_lament_battle.dusk`, copy it to Dusklight's mods
folder, and enable it in the Mods menu. Use the combined bundle from the
`mod-combined` Actions artifact, rather than an individual platform build.

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`
- Android/iOS: import the same combined bundle through the app's mod management.

The combined package supports Windows AMD64/ARM64, Linux x86_64/aarch64,
macOS Intel/Apple Silicon, Android aarch64, and iOS arm64. Build and static checks
are separate from in-game testing; consult the delivered validation reports.

## Audio

One four-channel PCM16 AST contains base L/R and combat L/R, sharing a playback
cursor and loop. The existing audio, loop boundaries and common gain adjustment
are unchanged in 1.0.2. The prepared AST is committed for identical CI builds;
original WAVs are not included. Tools for regenerating it from the original
aligned 48 kHz stereo stems are under `tools/`.

## Build

The SDK is pinned to Dusklight v2.0.3, commit
`40457c6adb381928e4b5fef6ed459ed291edd5e2`. Build infrastructure is adapted from
TwilitRealm/mod-template at `10cd79ed4210aaffadb554aac5f3d61476132539`.

With CMake 3.26+, Ninja and a platform C++20 compiler:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Local builds support only their target platform. For distribution, use the
included GitHub Actions workflow. It builds eight libraries, checks each against
the matching official 2.0.3 release, and merges them using `symgen modmeta --check
--update-json`. Missing platforms, conflicting service metadata, altered audio,
and unavailable required hooks fail the build. Native controller tests run on
Windows AMD64, Linux x86_64/aarch64 and macOS arm64. Cross-target jobs compile the
mod without trying to execute foreign test binaries.

All optional validation drivers default OFF and are excluded from release builds.
The workflow uploads build artifacts only; it does not create releases or upload
to the mod page. Original game files, saves and user configuration must never be
committed. Runtime tests use an extracted game copy and an explicit disposable
`--user-dir`, `--mods` and `--log-dir`.

## Terms and credits

Personal, non-commercial use only for the mod's own material. Do not redistribute,
reupload or sell it. Link to the original mod page instead. See `LICENSE` and
`THIRD_PARTY_NOTICES.md` for the scope of these terms and third-party exclusions.
The arrangement is by Wesley Damaschino; no ownership of the audio or original
Twilight Princess music is claimed.
