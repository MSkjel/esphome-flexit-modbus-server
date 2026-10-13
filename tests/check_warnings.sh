#!/usr/bin/env bash
# Fails if the build log has compiler warnings
# Usage: check_warnings.sh <build log>
set -euo pipefail

log="${1:?usage: check_warnings.sh <build log>}"

# The log has \r progress lines and colour codes, strip both before matching.
warnings=$(tr '\r' '\n' < "$log" \
  | sed 's/\x1b\[[0-9;]*[mK]//g' \
  | grep -E '(flexit_modbus_server\.(cpp|h)|\.ya?ml):[0-9]+:[0-9]+: warning:' \
  | sort -u || true)

if [ -n "$warnings" ]; then
  echo "Warnings in our code:"
  # ::error:: makes GitHub show them on the run summary
  echo "$warnings" | sed 's/^/::error::/'
  exit 1
fi

echo "No warnings in our code."
