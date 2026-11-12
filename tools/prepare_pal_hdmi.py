#!/usr/bin/env python3
"""Generate a reviewed PAL adaptation without writing into lib/."""
import hashlib
import pathlib
import sys

source, destination = map(pathlib.Path, sys.argv[1:])
data = source.read_bytes()
# Git blob identity at pico_hdmi f8034c7ae5e4927692229d114b84b6e81cc789d9.
blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
EXPECTED_BLOB = "a66f83470aa87248bfd61bfdb26d57e48a8f0691"
if blob != EXPECTED_BLOB:
    raise SystemExit(f"Unreviewed pico_hdmi backend {blob}; expected {EXPECTED_BLOB}")
text = data.decode()
old = "uint8_t vic = (mode->v_active_lines == 480) ? 1 : (mode->v_active_lines == 720) ? 4 : 0;"
new = """uint8_t vic = (mode->h_active_pixels == 720 && mode->v_active_lines == 576 &&
                   mode->h_total_pixels == 864 && mode->v_total_lines == 625) ? 17 :
                  (mode->v_active_lines == 480) ? 1 :
                  (mode->v_active_lines == 720) ? 4 : 0;"""
assert text.count(old) == 1
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(text.replace(old, new))
