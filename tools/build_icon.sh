#!/bin/sh
# Regenerate the Indemnity Run app icon from real game renders and install it to
# every platform target. Needs the quad-res host build (build_host).
#
#   tools/build_icon.sh
#
# Palette is the ship's: grey hull / purple cockpit / gold cannon tips.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QUAD="$ROOT/build_host/thumbyelite_host_quad"

ELITE_ICONCLS=3 ELITE_APPICON=1 "$QUAD" 1  >/dev/null 2>&1   # VIPER hull, 3/4 banked
ELITE_ICONBG=1               "$QUAD" 13 >/dev/null 2>&1      # gold world + purple nebula

python3 "$ROOT/tools/make_icon.py"    /tmp/icon
python3 "$ROOT/tools/install_icons.py"
