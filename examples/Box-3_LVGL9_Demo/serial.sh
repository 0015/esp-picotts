#!/bin/sh
set -Eeuo pipefail

# Exit with: Ctrl+t q
tio -b 115200 /dev/ttyACM0
