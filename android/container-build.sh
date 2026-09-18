#!/bin/sh
set -eu
android_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_dir=$(CDPATH= cd -- "$android_dir/.." && pwd -P)
image=${TURMITE_DOCKER_IMAGE:-turmite-android-build:local}
cache_dir="$android_dir/.docker-cache"
output_dir="$android_dir/app/build/outputs"

command -v docker >/dev/null 2>&1 || { echo 'Docker CLI is required.' >&2; exit 1; }
security=$(docker info --format '{{json .SecurityOptions}}') || {
    echo 'Cannot reach Docker. Start the daemon and check access to its socket.' >&2
    exit 1
}

# Standard Docker maps this UID/GID directly. Rootless Docker maps container root
# to the invoking user, so select root inside that user namespace instead.
case "$security" in
    *rootless*) build_user=0:0 ;;
    *) build_user="$(id -u):$(id -g)" ;;
esac

docker build --platform linux/amd64 --tag "$image" --file "$android_dir/Dockerfile" "$android_dir"
if [ "${1:-}" = --image-only ]; then
    [ "$#" -eq 1 ] || { echo '--image-only takes no other arguments.' >&2; exit 2; }
    exit 0
fi
mkdir -p "$cache_dir" "$output_dir"
if [ "$#" -eq 0 ]; then set -- assembleDebug; fi
# Lowercase z permits shared SELinux labels for the overlapping repo/cache mounts.
exec docker run --rm --init --platform linux/amd64 \
    --user "$build_user" \
    --volume "$repo_dir:/source:ro,z" \
    --volume "$cache_dir:/cache:z" \
    --volume "$output_dir:/output:z" \
    "$image" "$@"
