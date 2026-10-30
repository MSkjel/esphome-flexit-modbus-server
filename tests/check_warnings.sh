#!/usr/bin/env bash
# Fails if the build log has compiler warnings
# Usage: check_warnings.sh <build log>
set -euo pipefail

log="${1:?usage: check_warnings.sh <build log>}"

# A log without our main.cpp compiled would pass for the wrong reason
if ! grep -q 'main\.cpp' "$log"; then
  echo "The build log doesn't show main.cpp compiled"
  exit 1
fi

# The log has \r progress lines and colour codes, strip both before matching.
all=$(tr '\r' '\n' < "$log" | sed 's/\x1b\[[0-9;]*[mK]//g' | grep -E ':[0-9]+:[0-9]+: warning:' || true)
# Ours: the components, YAML lambdas, and the package code ESPHome copies into the build's src/ (not its own
# src/esphome/). That's src/... on Arduino and ../src/... on ESP-IDF, whose own sources have absolute paths.
warnings=$({
  grep -E '(flexit_modbus_server|panel_camera)\.(cpp|h):|\.ya?ml:' <<< "$all"
  grep -E '^(\.\./)?src/|/\.esphome/build/[^/]+/src/' <<< "$all" | grep -v 'src/esphome/'
} | sort -u || true)

if [ -n "$warnings" ]; then
  echo "Warnings in our code:"
  # ::error:: makes GitHub show them on the run summary
  echo "$warnings" | sed 's/^/::error::/'
  exit 1
fi

echo "No warnings in our code."
