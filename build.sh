#!/bin/sh
# Runs idf.py for the bootloader project in the pinned ESP-IDF image.
set -e
cd "$(dirname "$0")"
exec docker run --rm -v "$PWD":/project -w /project/bootloader -e HOME=/tmp espressif/idf:v4.4.7 \
    idf.py -B build-docker "$@"
