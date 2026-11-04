#!/bin/bash
# FellowEngine platform checks (milestone 49): the bench labs on simulated GPUs (FELLOW_VULKAN_PROFILE, decision 0150),
# Android's profiles with the Mobile renderer, the desktop GPUs with Forward+, on lavapipe.
#   profile_matrix.sh <out dir> [profile ...]
# Writes <out>/<profile>/ (renderer_matrix.sh's output) and <out>/portability.txt: every FELLOW_PORTABILITY line of
# every run, with the number of runs that printed it. Environment as renderer_matrix.sh (FELLOW_BIN, FELLOW_PROJECT).
set -u
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OUT=${1:?out dir}
shift
PROFILES=("$@")
if [ ${#PROFILES[@]} -eq 0 ]; then
	PROFILES=(android_2022 android_2025 intel_uhd620 intel_arc_a770 gtx1060 rtx4070 rx580 rx7800xt apple_m1)
fi
SCENES=(pbr:pbr_overview fire:fire_close water:water_shore "rain:rain_street:--weather=storm" wind:wind_meadow
	crowd:crowd_aerial streaming:street_start matching:matching_aerial)
mkdir -p "$OUT"
for p in "${PROFILES[@]}"; do
	case $p in
	android_*) renderers=mobile ;;
	*) renderers=forward_plus ;;
	esac
	PROFILE=$p RENDERERS=$renderers "$HERE/renderer_matrix.sh" "$OUT/$p" "${SCENES[@]}" | sed "s/^/$p\t/"
done
cat "$OUT"/*/*/*.portability.txt 2>/dev/null | sed -E 's/^FELLOW_PORTABILITY //' | sort | uniq -c | sort -rn > "$OUT/portability.txt"
