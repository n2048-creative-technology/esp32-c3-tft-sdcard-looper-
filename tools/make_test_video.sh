#!/usr/bin/env bash
# Generate a random 128x160 MJPEG .avi test video for the display (needs ffmpeg).
#
# Usage: tools/make_test_video.sh [output.avi] [seconds] [fps] [seed]
#
# Content is a Game-of-Life simulation from a random starting pattern (a new
# one each run unless you pass a seed), with a white 1-pixel border to check
# alignment and a box sliding along the bottom so dropped frames are visible.
set -euo pipefail

out=${1:-test.avi}
secs=${2:-10}
fps=${3:-15}
seed=${4:-$RANDOM}

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "life=s=128x160:rate=${fps}:ratio=0.35:mold=10:seed=${seed}:life_color=#ffcc00:death_color=#101040:mold_color=#ff2060" \
  -t "$secs" \
  -vf "drawbox=x=0:y=0:w=iw:h=ih:color=white:t=1,drawbox=x='mod(t*40,iw-12)':y=ih-14:w=12:h=12:color=cyan:t=fill" \
  -an -c:v mjpeg -q:v 5 -pix_fmt yuvj420p \
  "$out"

echo "Wrote $out (128x160, ${secs}s @ ${fps} fps, seed ${seed})"
