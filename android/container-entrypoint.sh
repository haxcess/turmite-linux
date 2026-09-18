#!/bin/sh
set -eu
umask 022
mkdir -p /cache/home/.android /cache/gradle /cache/work/android /cache/work/src
# A shared cache/work tree supports one build at a time.
exec 9>/cache/build.lock
flock 9

# Separate workspace avoids local.properties SDK paths and host CMake caches.
# Excluded build products remain cached; removed source files are deleted.
rsync -a --no-owner --no-group --delete \
    --exclude='.gradle/' --exclude='.idea/' --exclude='.cxx/' \
    --exclude='build/' --exclude='.docker-cache/' --exclude='local.properties' \
    --exclude='*.jks' --exclude='*.keystore' \
    /source/android/ /cache/work/android/
rsync -a --no-owner --no-group --delete --exclude='*.o' --exclude='*.d' \
    /source/src/ /cache/work/src/

# Reports are copied even on failure; a failed build never reports success.
finish() {
    result=$?
    trap - EXIT
    if [ -d /cache/work/android/app/build/outputs ]; then
        rsync -a --no-owner --no-group /cache/work/android/app/build/outputs/ /output/ || result=1
    fi
    if [ -d /cache/work/android/app/build/reports ]; then
        mkdir -p /output/reports
        rsync -a --no-owner --no-group /cache/work/android/app/build/reports/ /output/reports/ || result=1
    fi
    exit "$result"
}
trap finish EXIT
if [ "$#" -eq 0 ]; then set -- assembleDebug; fi
sh /cache/work/android/build.sh --no-daemon "$@"
