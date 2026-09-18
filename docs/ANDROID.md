# Android

Standalone screensaver (`DreamService`) and phone/TV preview launcher. Package: `org.haxcess.turmite`. Android 6.0/API 23 minimum; ARM32, ARM64, x86-64. No SDL, XScreensaver dependency, network permission, or capture ring.

## Build

Install JDK 17, Gradle 8.11.1, and Android SDK command-line tools or Android Studio. Set `JAVA_HOME`, add Gradle to `PATH`, and set `ANDROID_HOME` (or `sdk.dir` in `android/local.properties`). No Gradle wrapper is bundled in this initial scaffold.

```sh
sdkmanager --licenses
sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'platform-tools' 'ndk;28.0.13004108' 'cmake;3.22.1'
sh android/build.sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

AGP 8.9.2; compile/target SDK 35. Initial builds download Gradle plugin dependencies. Debug APK is locally signed; release signing/distribution is not configured. Open `android/` in Android Studio for IDE builds with Gradle 8.11.1 configured locally.

Launch **Turmite Universe** → **Preview**, or **Screensaver settings** → select **Turmite Universe**. Settings availability and idle/charging activation depend on device firmware; verify on the Bravia and phone. The app does not change the selected saver automatically. Dream input exits normally; preview exits with Back.

## Shared sources

`android/app/src/main/cpp/CMakeLists.txt` compiles repository `src/{ant,rules,rng,world,scheduler,renderer}.c` directly. Edit `src/rules.c`, then rebuild Linux with `make` and Android with `sh android/build.sh`. Each binary embeds the catalogue at build time. No generated Android catalogue or copied simulator.

| Layer | Ownership |
| --- | --- |
| `TurmiteDreamService` / `PreviewActivity` | Android lifecycle |
| `TurmiteView` frame thread | Native handle, snapshot conversion, bitmap, surface canvas |
| `engine.c` | World, colony, scheduler, two pthread workers |
| Shared C sources | Rules, mutations, scheduling, logical tape, palette |

Android defaults: 8 ants, quantum 64, minimum service 16, divisor 1, five-minute reseeding, ~30 FPS. These host settings are independent of Linux CLI defaults. Canvas preserves aspect ratio, caps longest side at 960 cells, and scales with nearest-neighbor sampling. No HUD or settings editor yet.

Lifecycle stop/surface destruction interrupts and joins the frame thread; native destruction stops the scheduler and joins ant workers before releasing state. Resize creates a fresh universe. Native state is 64-byte aligned for `AntColony`. Frame samples remain non-transactional. Bitmap/JNI copies are an initial backend; profile on hardware before increasing resolution.

## Validation

Native host tests require CMake and a C17 compiler, without Java/SDK:

```sh
cmake -S android/app/src/main/cpp -B /tmp/turmite-android-host -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/turmite-android-host
ctest --test-dir /tmp/turmite-android-host --output-on-failure
```

Coverage: repeated create/render/destroy, worker progress, shared palette, invalid bounds. Android build/lint: `sh android/build.sh assembleDebug lintDebug`.

Device checks: phone/TV launcher, D-pad navigation, system dream preview, normal idle activation, input exit, repeated start/stop, orientation/surface recreation, worker shutdown, sustained temperature/CPU use. Verify ARM32/ARM64 and 16 KB page-size compatibility. No device or APK validation has been performed for this scaffold.

## References

- [DreamService lifecycle and manifest](https://developer.android.com/reference/android/service/dreams/DreamService)
- [Android native builds](https://developer.android.com/studio/projects/add-native-code)
- [AGP 8.9 compatibility](https://developer.android.com/build/releases/agp-8-9-0-release-notes)
- [16 KB native-library support](https://developer.android.com/guide/practices/page-sizes)
- [TV manifest requirements](https://developer.android.com/training/tv/get-started/create)
