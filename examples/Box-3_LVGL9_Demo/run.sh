#!/bin/sh
set -Eeuo pipefail

idf.py flash

./serial.sh
