# Docker Android build

Host requirement: Docker CLI and a running daemon. No local JDK, Gradle, SDK, NDK, or CMake installation.

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
| Base | `eclipse-temurin:21-jdk-jammy` |
| Gradle | 8.11.1 |
| Android command-line tools | 15859902 |
| Platform / Build Tools | 35 / 35.0.0 |
| NDK | 28.0.13004108 |
| CMake | 3.22.1 |

Gradle and command-line archives have verified SHA-256 checksums. Base-image security updates and Platform Tools are not digest/version locked. Image builds use `linux/amd64` for the Linux NDK host tools; ARM hosts require Docker x86-64 emulation. APKs still include ARM32, ARM64, and x86-64 device libraries.

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

```sh
adb -s DEVICE_SERIAL install -r android/app/build/outputs/apk/debug/app-debug.apk
```

Use host adb or transfer the APK separately. Build containers require no USB access, privileged mode, or Docker socket mount. Release signing remains unconfigured.

Docker unavailable: check `docker version` and daemon/socket access. SDK-version errors: keep the Dockerfile packages synchronized with `android/app/build.gradle`. Run with `--stacktrace` for Gradle diagnostics.

Validation of this addition: shell syntax and mocked launcher/entrypoint tests. Image download/build, Gradle compilation, and device execution require a Docker-enabled host and remain unverified here.

[Gradle checksums](https://gradle.org/release-checksums/) · [Android tools/checksums](https://developer.android.com/studio#command-tools)
