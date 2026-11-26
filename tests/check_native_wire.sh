#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
main=firmware/devices/sensecap-indicator/targets/examples/button_backlight/main
speaker=audio/respeaker/devices/respeaker-lite/targets/examples/audio_meter/main
cmp "$main/fav1/av_adpcm.c" "$speaker/fav1/av_adpcm.c"
cmp "$main/fav1/av_adpcm.h" "$speaker/fav1/av_adpcm.h"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I "$main" -I "$main/fav1" -I "$main/cjson" \
    tests/native_wire.c "$main/av_picture.c" "$main/fav1/av_protocol.c" \
    "$main/fav1/av_adpcm.c" "$main/playback_profile.c" "$main/cjson/cJSON.c" -lz -lm -o "$scratch/native-wire"
PYTHONDONTWRITEBYTECODE=1 python3 check_upstream.py --native-check "$scratch/native-wire" "$@"
