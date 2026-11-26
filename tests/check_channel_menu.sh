#!/bin/sh
set -eu
cd "$(dirname "$0")"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
source_dir=../firmware/devices/sensecap-indicator/targets/examples/button_backlight/main
speaker_dir=../audio/respeaker/devices/respeaker-lite/targets/examples/audio_meter/main
cmp "$source_dir/relay_volume.h" "$speaker_dir/relay_volume.h"
python3 - "$source_dir/ui/ui_font_cjk.bin" "$temporary/font.c" <<'PY'
from pathlib import Path
import sys
bits = Path(sys.argv[1]).read_bytes()
with Path(sys.argv[2]).open("w") as out:
    out.write('const unsigned char blob[] __asm__("_binary_ui_font_cjk_bin_start") = {\n')
    for i in range(0, len(bits), 32):
        out.write(",".join(map(str, bits[i:i + 32])) + ",\n")
    out.write("};\n")
PY
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I "$source_dir" -I "$source_dir/ui" \
    -I "$source_dir/fav1" -I "$source_dir/cjson" \
    test_channel_menu.c "$source_dir/channel_menu.c" "$source_dir/stream_status.c" "$source_dir/ui/ui_text.c" \
    "$source_dir/playback_profile.c" "$source_dir/fav1/av_channel_policy.c" "$source_dir/fav1/av_protocol.c" \
    "$source_dir/cjson/cJSON.c" "$temporary/font.c" -lm -o "$temporary/channel-menu"
"$temporary/channel-menu"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I "$source_dir" -I "$source_dir/fav1" -I "$source_dir/cjson" \
    test_setup_portal.c "$source_dir/setup_portal_config.c" "$source_dir/fav1/tv_config.c" \
    "$source_dir/fav1/av_protocol.c" "$source_dir/cjson/cJSON.c" -lm -o "$temporary/setup-portal"
"$temporary/setup-portal"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I stubs -I "$source_dir" -I "$source_dir/fav1" \
    test_session_connection.c "$source_dir/session_connection.c" -o "$temporary/session-connection"
"$temporary/session-connection"
