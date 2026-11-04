#!/bin/bash
# The CMU Graphics Lab Motion Capture Database's locomotion (subjects 16, 35 and 69: walking, jogging, running, turns,
# stops, side and back steps) in Bruce Hahne's BVH conversion, for Matching Lab (benchmarks/project/scenes/
# matching_lab.gd, milestone 48). CMU: "This data is free for use in research and commercial projects worldwide"; the
# conversion: "no additional restrictions" (its READMEFIRST.txt). Not part of this repository: fetched into
# benchmarks/project/external/matching/cmu (ignored by git), 144 MB.
#   get_cmu_locomotion.sh [destination]
set -e
DEST=${1:-$(cd "$(dirname "$0")/.." && pwd)/project/external/matching/cmu}
TMP=$(mktemp -d)
git clone --depth 1 --filter=blob:none --sparse https://github.com/una-dinosauria/cmu-mocap.git "$TMP/cmu"
git -C "$TMP/cmu" sparse-checkout set --no-cone READMEFIRST.txt cmu-mocap-index-text.txt 'data/016/*' 'data/035/*' 'data/069/*'
mkdir -p "$DEST"
cp -r "$TMP/cmu/data/016" "$TMP/cmu/data/035" "$TMP/cmu/data/069" "$TMP/cmu/READMEFIRST.txt" "$TMP/cmu/cmu-mocap-index-text.txt" "$DEST/"
rm -rf "$TMP"
echo "CMU locomotion in $DEST"
