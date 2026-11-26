#!/bin/sh
set -eu
cd "$(dirname "$0")"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
main=../firmware/devices/sensecap-indicator/targets/examples/button_backlight/main
speaker=../audio/respeaker/devices/respeaker-lite/targets/examples/audio_meter/main
cmp "$main/relay_timing.h" "$speaker/relay_timing.h"
cmp "$main/relay_link.h" "$speaker/relay_link.h"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I "$main" -I "$speaker" -I "$speaker/fav1" \
    test_video_clock.c "$speaker/audio_playout.c" "$speaker/fav1/av_protocol.c" "$speaker/fav1/av_adpcm.c" \
    -o "$scratch/video-clock"
"$scratch/video-clock"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I "$main" -I "$main/fav1" \
    test_reconnect_view.c "$main/reconnect_view.c" -o "$scratch/reconnect-view"
"$scratch/reconnect-view"
sh ../audio/tests/check_audio.sh
