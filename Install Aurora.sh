#!/bin/sh
# ArkOS Ports menu entry for installing/registering Aurora.
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec /bin/sh "$HERE/Aurora-R36/install.sh"
