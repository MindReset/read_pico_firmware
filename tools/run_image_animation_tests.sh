#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root/main/ui" -I"$root/main/book" \
  "$root/tools/image_animation_host_test.c" \
  "$root/main/ui/ui_image_dither.c" \
  -o /tmp/read-pico-image-animation-test
/tmp/read-pico-image-animation-test
python3 - "$root" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
source = (root / "main/settings.c").read_text()
header = (root / "main/settings.h").read_text()
book = (root / "main/apps/app_book.c").read_text()
assert 'NVS_KEY_BOOK_ANIM' not in source
assert 'app_settings_book_animation' not in header
assert 'present_page_animation' not in book
assert '"翻页动画"' not in book
assert 'return paint_reading(ctx, MODE_GL16);' in book
assert 'static uint8_t s_shelf_style = 2;' in source
assert 'NVS_KEY_SHELF_V22' in source
print("animation removal and acrylic shelf default contracts passed")
PY
