# Optimized Android release

The Linux Makefile and its `-g` flag do not control Android builds. Android uses Gradle and its own CMake project. Use the release variant for distribution; `assembleDebug` is for development.

## Build and inspect

From the repository root:

```sh
sh android/container-build.sh verifyReleaseApk lintRelease
```

This builds `android/app/build/outputs/apk/release/app-release-unsigned.apk` and checks the actual APK for a non-debuggable manifest, stripped native libraries for all three ABIs, preserved JNI entrypoints, unexpected source/build/signing files, and 16 KB ZIP alignment. Treat any failed check or lint task as a failed release build. Existing exported files can survive a failed build; do not publish those as new results.

Release configuration enables:

- R8 Java code optimization, shrinking and obfuscation, plus unused-resource shrinking.
- Java compilation without source, line-number or local-variable debug metadata.
- Native CMake `Release`, `-O3`, `NDEBUG`, `-g0`, link-time optimization, hidden internal symbols and unused-section removal.
- Native linker stripping and AGP packaging with no optional native debug-symbol archive.

JNI entrypoint names are preserved, as is the settings activity named in the dream XML. Required dynamic symbols, relocations, unwind data, manifest, resources and signing metadata are runtime/package requirements; removing these indiscriminately can break the app. R8 mapping files and build reports remain outside the APK. Preserve the mapping file privately for interpreting obfuscated crashes.

The universal APK includes ARM32, ARM64 and x86-64 for the supported phones/TVs. Those libraries are not debug overhead. For Play distribution, `sh android/container-build.sh bundleRelease lintRelease` also produces `android/app/build/outputs/bundle/release/app-release.aab`; Play can deliver only the device's ABI. The APK verifier checks the APK, not the AAB or Play-generated splits. No performance or size claim is made until the release has been built and measured on devices.

## Sign the APK using Docker

The release is deliberately unsigned until you supply your own persistent release key. Do not publish a debug-key-signed APK. The following commands use the already-built Docker image; no host JDK or Android SDK is needed.

1. Create a private key directory outside the repository and open a signing shell. These commands assume a Linux Docker host, as does the build launcher:

   ```sh
   mkdir -p "$HOME/.local/share/turmite-signing"
   chmod 700 "$HOME/.local/share/turmite-signing"
   signing_user="$(id -u):$(id -g)"
   case "$(docker info --format '{{json .SecurityOptions}}')" in
       *rootless*) signing_user=0:0 ;;
   esac
   docker run --rm -it --platform linux/amd64 \
       --user "$signing_user" --entrypoint /bin/sh \
       --env HOME=/tmp --env JAVA_TOOL_OPTIONS=-Duser.home=/tmp \
       --volume "$HOME/.local/share/turmite-signing:/keys:z" \
       --volume "$PWD/android/app/build/outputs/apk/release:/apk:z" \
       "${TURMITE_DOCKER_IMAGE:-turmite-android-build:local}"
   ```

2. Inside that container, generate a key **once**, if you do not already have a release key. Answer the interactive identity/password prompts. Use your existing key for updates:

   ```sh
   umask 077
   keytool -genkeypair -keystore /keys/turmite-release.jks \
       -alias turmite -keyalg RSA -keysize 3072 -validity 10000
   ```

   Back up this key and its password securely. Losing them can prevent updates to existing installations. Do not put the password in shell arguments or commit the key.

3. Still inside the container, align, sign, and verify. Adjust the keystore/alias if using an existing key:

   ```sh
   /opt/android-sdk/build-tools/35.0.0/zipalign -P 16 -f 4 \
       /apk/app-release-unsigned.apk /apk/app-release-aligned.apk
   /opt/android-sdk/build-tools/35.0.0/apksigner sign \
       --ks /keys/turmite-release.jks --ks-key-alias turmite \
       --out /apk/turmite-release.apk /apk/app-release-aligned.apk
   /opt/android-sdk/build-tools/35.0.0/apksigner verify --verbose /apk/turmite-release.apk
   /opt/android-sdk/build-tools/35.0.0/zipalign -c -P 16 4 /apk/turmite-release.apk
   exit
   ```

   The signed APK is now `android/app/build/outputs/apk/release/turmite-release.apk`. Do not alter its contents after signing. AAB signing/Play upload is a separate workflow; `apksigner` signs APKs only.

4. Install the signed release and test preview, settings, dream activation, rotation and repeated stop/start on the target phone/TV. A release key cannot update an installation signed with a different debug key; uninstalling the old app clears its settings. Increment `versionCode` for subsequent published updates.

Release optimization and stripping do not by themselves establish store eligibility. Store target-SDK requirements and release signing must be checked for the intended distribution channel.

## Validation status

On 2026-11-13, the Docker build completed `assembleDebug lintDebug verifyReleaseApk lintRelease --rerun-tasks` successfully, including Gradle/R8, NDK compilation for all three ABIs, lint and actual release APK verification. The native host lifecycle test also passed. Release signing and device execution remain unverified.

References: [R8 optimization](https://developer.android.com/topic/performance/app-optimization/enable-app-optimization), [native symbols](https://developer.android.com/build/include-native-symbols), [APK signing](https://developer.android.com/tools/apksigner), [alignment](https://developer.android.com/tools/zipalign).
