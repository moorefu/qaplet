#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_dir/../scripts/nightly_toolchain.py" fetch "$@"
