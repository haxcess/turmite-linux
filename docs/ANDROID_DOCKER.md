# Docker Android build

Host requirement: Docker CLI and a running daemon. No local JDK, Gradle, SDK, NDK, or CMake installation.

The build uses the published [`thyrlian/android-sdk:10.0`](https://hub.docker.com/r/thyrlian/android-sdk/tags) Java/Android SDK image ([upstream source](https://github.com/thyrlian/AndroidSDK/blob/master/android-sdk/Dockerfile)). Docker adds a project-specific layer containing Gradle 8.11.1, SDK 35, NDK 28, CMake, and our build entrypoint. This is a prebuilt SDK base, not a published, ready-to-run Turmite builder. The first build still downloads the project-specific packages; later builds reuse them.

## First build, step by step

Run these commands in a terminal on the machine where Docker is installed.

1. Open the repository and check Docker:

   ```sh
   cd /home/alex/Coding/turmite/turmite-linux
   docker version
   docker info
   ```

   Both client and server must be reachable. If the server is unavailable, start Docker Desktop or your Docker Engine service. On a Linux system using systemd, `sudo systemctl start docker` starts an installed Engine. If access is denied, use your configured Docker context or resolve daemon permissions before continuing. Do not run the build script with `sudo`, since it would select root ownership for the output.

2. Prepare the build image:

   ```sh
   sh android/container-build.sh --image-only
   ```

   This pulls the prebuilt base and installs the pinned project tools. It needs internet access and several GB of free disk space. Building the image accepts the Android SDK licenses linked below.

3. Build and lint the debug APK:

   ```sh
   sh android/container-build.sh assembleDebug lintDebug
   ```

   Wait for `BUILD SUCCESSFUL` and a zero exit status. The first Gradle run downloads dependencies. To save a diagnostic log while preserving the exit status:

   ```sh
   sh android/container-build.sh assembleDebug lintDebug --stacktrace > /tmp/turmite-android-build.log 2>&1
   build_status=$?
   cat /tmp/turmite-android-build.log
   test "$build_status" -eq 0
   ```

4. Find the APK at `android/app/build/outputs/apk/debug/app-debug.apk`. Continue with the installation steps below.

## Subsequent builds

```sh
sh android/container-build.sh
sh android/container-build.sh assembleDebug lintDebug
```

The first run builds the image and downloads toolchains/dependencies. Later runs reuse Docker layers and Gradle/native caches. Building the image accepts the [Android SDK licenses](https://developer.android.com/studio#command-tools).

Output:

```text
android/app/build/outputs/apk/debug/app-debug.apk
android/app/build/outputs/reports/
```

Use `sh android/container-build.sh --image-only` to prepare only the image. Other arguments pass to Gradle. Default image tag: `turmite-android-build:local`; override with `TURMITE_DOCKER_IMAGE`. Source is synced from the current working tree, including uncommitted rule changes. Linux sources remain shared; rebuild each platform separately.

## Toolchain

| Component | Version |
| --- | --- |
| Prebuilt SDK base | `thyrlian/android-sdk:10.0` (JDK 17) |
| Gradle | 8.11.1 |
| Android command-line tools | Supplied by the prebuilt SDK base |
| Platform / Build Tools | 35 / 35.0.0 |
| NDK | 28.0.13004108 |
| CMake | 3.22.1 |

The added Gradle archive has a verified SHA-256 checksum. The prebuilt base is tag-pinned, not digest-pinned; its toolchain comes from the upstream image. Platform Tools are not version locked. Image builds use `linux/amd64` for the Linux NDK host tools; ARM hosts require Docker x86-64 emulation. APKs still include ARM32, ARM64, and x86-64 device libraries.

## Workspace and persistence

| Host path | Purpose |
| --- | --- |
| Repository | Read-only source mount |
| `android/.docker-cache/work/` | Isolated Gradle/CMake workspace |
| `android/.docker-cache/gradle/` | Gradle dependencies |
| `android/.docker-cache/home/.android/` | Persistent debug signing key |
| `android/app/build/outputs/` | Exported APKs and reports |

The launcher uses the host UID/GID, or container root mapped to the host user under rootless Docker. Fedora mounts use shared SELinux `:z` labels; SELinux stays enabled. One build per cache runs at a time. Host `local.properties`, SDK paths, signing files, and build caches are excluded from source sync.

Keep `.docker-cache/home/.android/debug.keystore`: replacing it changes the debug signing identity and prevents updating an installed APK signed with the old key. Container and local builds have separate keys unless explicitly migrated. `clean` clears the container build tree, not exported host artifacts; check command success and APK timestamps before installing. Do not run local and container builds concurrently into the same host output directory.

## Install and diagnose

You can transfer the APK to the phone/TV and open it using an installer, allowing installation from that source when Android prompts. Alternatively, use `adb` from Android Platform Tools on your host; it is not required for building.

1. On the phone/TV, enable Developer options (usually by selecting **Build number** seven times), then enable **USB debugging**. Menu names vary by manufacturer. Connect a USB data cable and approve the computer's debugging prompt on the device. For a TV without a USB debugging connection, use its supported wireless debugging/pairing procedure.
2. Check the connection:

   ```sh
   adb devices -l
   ```

   The device must show `device`, not `unauthorized` or `offline`.
3. Replace `DEVICE_SERIAL` with the serial shown above and install:

   ```sh
   adb -s DEVICE_SERIAL install -r android/app/build/outputs/apk/debug/app-debug.apk
   ```

4. Open **Turmite Universe** in the phone/TV launcher and select **Preview**. Check animation, settings, and Back navigation.
5. Open the app's **Screensaver settings**, select **Turmite Universe**, and use the system preview if available. Configure idle/charging activation in the device settings. Availability depends on the device firmware.
6. Test repeated preview start/stop and normal screensaver activation. On a phone, rotate the preview; on a TV, check D-pad navigation. Report the device model, Android version, and any failure.

Keep the persistent debug signing key described above so subsequent builds can update the installed app. Build containers require no USB access, privileged mode, or Docker socket mount. Release signing remains unconfigured.

Docker unavailable: check `docker version` and daemon/socket access. SDK-version errors: keep the Dockerfile packages synchronized with `android/app/build.gradle`. Run with `--stacktrace` for Gradle diagnostics.

Validation of this addition: shell syntax and mocked launcher/entrypoint tests. Image download/build, Gradle compilation, and device execution require a Docker-enabled host and remain unverified here.

[Gradle checksums](https://gradle.org/release-checksums/) · [Android tools](https://developer.android.com/studio#command-tools) · [Device debugging](https://developer.android.com/studio/run/device)
