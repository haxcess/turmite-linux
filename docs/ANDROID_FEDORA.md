# Fedora Android build environment

With Docker installed, use [the container build](ANDROID_DOCKER.md). The local installations below are optional.

## Versions

| Component | Version |
| --- | --- |
| JDK | 21 (recommended) or 17; Java source/target remains 17 |
| Gradle | 8.11.1 |
| Android Gradle plugin | 8.9.2, fetched by Gradle |
| SDK platform / Build Tools | 35 / 35.0.0 |
| NDK | 28.0.13004108 |
| SDK CMake | 3.22.1 |
| Platform Tools | Current; includes adb |

Use an explicit JDK version: newer Fedora default Java versions may exceed Gradle 8.11.1 support. [Gradle Java compatibility](https://docs.gradle.org/8.11.1/userguide/compatibility.html), [AGP requirements](https://developer.android.com/build/releases/agp-8-9-0-release-notes).

## Install

Host tools (C compiler/CMake/Ninja are for native host tests):

```sh
sudo dnf install git curl unzip zip gcc make cmake ninja-build
```

Install a Linux JDK 21 from [Eclipse Temurin](https://adoptium.net/temurin/releases/?version=21), matching the host architecture. Extract under `~/.local/share/jdks/` and set `JAVA_HOME` to the extracted directory. JDK 17 also supports this Gradle build. Do not assume Fedora 44 provides the older `java-17-openjdk-devel` package.

Download [Gradle 8.11.1 binary ZIP](https://services.gradle.org/distributions/gradle-8.11.1-bin.zip). Verify SHA-256 against the [published checksum](https://gradle.org/release-checksums/):

```text
f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6
```

Extract into `~/.local/share/gradle/`, producing `gradle-8.11.1/bin/gradle`.

Install [Android Studio or Linux command-line tools](https://developer.android.com/studio). Studio is optional. For command-line tools, arrange the extracted package as:

```text
~/Android/Sdk/cmdline-tools/latest/bin/sdkmanager
~/Android/Sdk/cmdline-tools/latest/lib/...
~/Android/Sdk/cmdline-tools/latest/source.properties
```

Set in the shell profile, replacing the JDK directory with its actual name:

```sh
export JAVA_HOME="$HOME/.local/share/jdks/YOUR_JDK_21_DIRECTORY"
export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$JAVA_HOME/bin:$HOME/.local/share/gradle/gradle-8.11.1/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
```

Install SDK packages and review licenses:

```sh
sdkmanager --licenses
sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'platform-tools' 'ndk;28.0.13004108' 'cmake;3.22.1'
java -version
javac -version
gradle --version
adb version
```

Use the SDK's NDK/Clang for APK compilation; host GCC and Fedora CMake do not replace the pinned SDK packages. No SDL, Kotlin toolchain, XScreensaver source, emulator, or system-wide Gradle package is required. Android Studio users can install the same versions through SDK Manager with **Show Package Details** enabled.

## Build and install

From repository root:

```sh
sh android/build.sh assembleDebug lintDebug
adb devices
adb -s DEVICE_SERIAL install -r android/app/build/outputs/apk/debug/app-debug.apk
```

Enable device Developer options/USB or wireless debugging and authorize the host. With multiple devices, use `-s`. Select Turmite in system screensaver settings. TV settings exposure depends on firmware. No system changes or downloads are performed by applying the settings patch.
