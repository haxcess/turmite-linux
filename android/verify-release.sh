#!/bin/sh
# Run by :app:verifyReleaseApk inside the SDK build environment.
set -eu
if [ "$#" -ne 3 ]; then
    echo 'Usage: verify-release.sh APK NDK_DIR SDK_DIR' >&2
    exit 2
fi
apk=$1
ndk_dir=$2
sdk_dir=$3
readelf="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
analyzer="$sdk_dir/cmdline-tools/tools/bin/apkanalyzer"
if [ ! -x "$analyzer" ]; then
    analyzer="$sdk_dir/cmdline-tools/latest/bin/apkanalyzer"
fi
[ -f "$apk" ] || { echo "Missing APK: $apk" >&2; exit 1; }
[ "$("$analyzer" manifest debuggable "$apk")" = false ] || {
    echo 'Release APK is debuggable or could not be inspected.' >&2; exit 1;
}
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
unzip -Z1 "$apk" > "$work_dir/entries"
if grep -E '\.(c|h|java|o|a|keystore|jks)$|(^|/)(mapping\.txt|native-debug-symbols\.zip)$' "$work_dir/entries"; then
    echo 'Unexpected source, build, or signing material in APK.' >&2; exit 1
fi
for abi in armeabi-v7a arm64-v8a x86_64; do
    unzip -p "$apk" "lib/$abi/libturmite_android.so" > "$work_dir/lib.so"
    "$readelf" --sections --wide "$work_dir/lib.so" > "$work_dir/sections"
    if grep -E '\.(debug[^ ]*|zdebug[^ ]*|symtab|strtab|gnu_debug[^ ]*)[[:space:]]' "$work_dir/sections"; then
        echo "Debug information or static symbols remain in $abi." >&2; exit 1
    fi
    "$readelf" --dyn-syms --wide "$work_dir/lib.so" > "$work_dir/symbols"
    for method in create frame destroy; do
        grep -Eq "GLOBAL +DEFAULT +[0-9]+ +Java_org_haxcess_turmite_NativeEngine_${method}$" "$work_dir/symbols" || {
            echo "Missing JNI entrypoint: $abi/$method" >&2; exit 1;
        }
    done
    echo "Verified stripped native library: $abi"
done
"$sdk_dir/build-tools/35.0.0/zipalign" -c -P 16 4 "$apk"
echo 'Release APK checks passed (signing and device testing are separate).'
