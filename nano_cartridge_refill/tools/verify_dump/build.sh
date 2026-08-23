#!/bin/sh
# Build the dump verifier against the firmware's own codec -- the same code a
# restore would have to accept, so a PASS here means the image is genuinely
# restorable and not merely well-formed.
set -e
cd "$(dirname "$0")"
g++ -std=c++11 -Wall -I ../../lib/stratasys \
    ../../lib/stratasys/des.cpp \
    ../../lib/stratasys/stratasys_codec.cpp \
    ../../lib/stratasys/f64.cpp \
    verify_dump.cpp -o verify_dump
echo "built: $(pwd)/verify_dump"
