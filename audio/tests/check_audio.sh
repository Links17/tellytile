#!/bin/sh
set -eu
cd "$(dirname "$0")"
main=../respeaker/devices/respeaker-lite/targets/examples/audio_meter/main
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
for name in relay_audio audio_playout; do
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I "$main" -I "$main/fav1" \
        "${name}_test.c" "$main/audio_playout.c" "$main/fav1/av_protocol.c" \
        "$main/fav1/av_adpcm.c" -o "$scratch/$name"
    "$scratch/$name"
done
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I setup_stubs -I "$main" \
    speaker_setup_test.c "$main/speaker_setup.c" "$main/speaker_config.c" -o "$scratch/speaker-setup"
"$scratch/speaker-setup"
PYTHONDONTWRITEBYTECODE=1 python3 test_setup_speaker.py
