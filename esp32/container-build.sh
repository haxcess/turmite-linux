#!/bin/sh
set -eu
esp_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_dir=$(CDPATH= cd -- "$esp_dir/.." && pwd -P)
image=${TURMITE_IDF_IMAGE:-espressif/idf:v5.5.2}
command -v docker >/dev/null 2>&1 || { echo 'Docker CLI is required.' >&2; exit 1; }
security=$(docker info --format '{{json .SecurityOptions}}')
case "$security" in
    *rootless*) build_user=0:0 ;;
    *) build_user="$(id -u):$(id -g)" ;;
esac
# Serialize IDF operations sharing sdkconfig/build. Sources stay in place.
exec 9>"$esp_dir/.build.lock"
flock 9
[ "$#" -gt 0 ] || set -- build
needs_port=false
for arg do
    case "$arg" in
        flash|app-flash|bootloader-flash|partition-table-flash|monitor|backup) needs_port=true ;;
        erase-flash|erase_flash) echo 'Full flash erase is not supported by this launcher.' >&2; exit 2 ;;
    esac
done
if "$needs_port"; then
    : "${TURMITE_ESP32_PORT:?Set TURMITE_ESP32_PORT to the board serial device}"
    device=$(readlink -f -- "$TURMITE_ESP32_PORT")
    [ -c "$device" ] || { echo "Not a serial device: $device" >&2; exit 2; }
    if [ "$1" = backup ]; then
        [ "$#" -eq 1 ] || { echo 'backup takes no arguments' >&2; exit 2; }
        mkdir -p "$esp_dir/backups"
        filename="/project/esp32/backups/flash-$(date +%Y%m%d-%H%M%S).bin"
        set -- python -m esptool --chip esp32s3 --port /dev/ttyACM0 read_flash 0 0x1000000 "$filename"
    else
        set -- idf.py -p /dev/ttyACM0 "$@"
    fi
    set -- --device "$device:/dev/ttyACM0" --group-add "$(stat -c %g "$device")" "$image" "$@"
else
    set -- "$image" idf.py "$@"
fi
if [ -t 0 ] && [ -t 1 ]; then set -- -it "$@"; fi
exec docker run --rm --init --user "$build_user" \
    --env HOME=/tmp --env IDF_TARGET=esp32s3 --workdir /project/esp32 \
    --volume "$repo_dir:/project:ro,z" \
    --volume "$esp_dir:/project/esp32:z" \
    "$@"
