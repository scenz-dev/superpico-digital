#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hdmi=${SUPERPICO_HDMI_SOURCE_DIR:-"$repo/lib/pico_hdmi"}
out=${TMPDIR:-/tmp}/superpico-pal-tests-$$
trap 'rm -f "$out"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$repo/tests/stubs" -I"$repo/src/video" -I"$repo/src/audio" -I"$hdmi/include" \
  "$repo/tests/test_pal.c" "$repo/src/audio/src.c" -o "$out"
"$out"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$repo/tests/stubs" -I"$repo/src" -I"$repo/src/video" -I"$hdmi/include" \
  "$repo/tests/test_pipeline.c" "$repo/src/video/video_pipeline.c" "$repo/src/osd/fast_osd.c" -o "$out"
"$out"
