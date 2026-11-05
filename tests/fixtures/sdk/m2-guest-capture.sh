#!/bin/sh
# SPDX-License-Identifier: MIT
# Separate Host capture fixture. The product inherits only its actual stdio.
set -u
cd "$TMPDIR" || exit 125
"$@" >capture.stdout 2>capture.stderr
status=$?
printf 'M2G 1\nCWD %s\nEXIT %s\nSTDOUT ' "$PWD" "$status"
/bin/busybox base64 capture.stdout | /bin/busybox tr -d '\n'
printf '\nSTDERR '
/bin/busybox base64 capture.stderr | /bin/busybox tr -d '\n'
printf '\n'
for name in tty-native.ndjson tty-controlling-alias.json mixed-result.txt mixed-waits.ndjson mixed-terminal.ndjson mixed-child.json cli.stdout cli.stderr strong-child.json strong-child-arm strong-native-status.txt strong-native-identity.json queue-result.json controller-signals.ndjson signal-fault.ndjson signal-late.ndjson signal-native.ndjson late-arm direct-child.json mixed-ready mixed-native-status.txt mixed-native-identity.json cli-mixed-terminal.ndjson cli-terminal.ndjson cli-child-status.json cli-terminal-result.txt cli-pty.stdout.log text.ready cli-startup.ndjson forbidden-js cycles-progress.ndjson cycles-waits.ndjson; do
  if test -f "$name"; then
    printf 'SIDE %s ' "$name"
    /bin/busybox base64 "$name" | /bin/busybox tr -d '\n'
    printf '\n'
  fi
done
printf 'END\n'
exit "$status"
