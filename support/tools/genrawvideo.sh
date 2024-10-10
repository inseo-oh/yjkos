#!/bin/sh
# Generates series of raw video frames into a big binary.

if [ $# -lt 1 ]; then
    echo "Usage: genrawvideo.sh [Video file]"
    exit 1
fi

FILENAME=$1

ffmpeg -i $FILENAME -f rawvideo -pix_fmt rgb555 -vf "scale=640:480" $FILENAME.rawvideo