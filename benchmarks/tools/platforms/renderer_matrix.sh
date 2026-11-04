#!/bin/bash
# FellowEngine platform checks (milestone 49): every bench lab with each of Godot's renderers on Mesa's software drivers
# (Linux, no GPU): Forward+ and Mobile on lavapipe (Vulkan), Compatibility on llvmpipe (OpenGL 3.3). Mobile is what
# Godot runs on Android and Quest, Compatibility what it runs on the web and old GPUs: FellowEngine's nodes have to run
# there, or step aside, without crashing.
#   renderer_matrix.sh <out dir> [scene:camera[:extra args] ...]
# Writes <out>/<renderer>/<scene>/ (the capture and its log) and <out>/summary.tsv: exit status, seconds, errors (lines
# with ERROR or SCRIPT ERROR, unique messages in <scene>.errors.txt) and the image's mean and spread (a black or flat
# frame shows as a spread near 0). Environment: FELLOW_BIN, FELLOW_PROJECT (a project copy with external/ data for
# the matching, streaming and world labs), RES (480x270), SETTLE (30), RENDERERS ("forward_plus mobile
# gl_compatibility"), PROFILE (a simulated Vulkan device, FELLOW_VULKAN_PROFILE: its FELLOW_PORTABILITY lines go to
# <scene>.portability.txt and are counted in the summary). Each run has its own user data directory (XDG_DATA_HOME
# under <out>), so that no shader cache is shared between profiles.
set -u
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OUT=${1:?out dir}
shift
FELLOW_BIN=${FELLOW_BIN:-$HERE/../../../bin/godot.linuxbsd.template_release.x86_64}
FELLOW_PROJECT=${FELLOW_PROJECT:-/tmp/fellow_platforms/project}
RES=${RES:-480x270}
SETTLE=${SETTLE:-30}
RENDERERS=${RENDERERS:-forward_plus mobile gl_compatibility}
LVP_ICD=${LVP_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
if [ ! -d "$FELLOW_PROJECT" ]; then
	mkdir -p "$(dirname "$FELLOW_PROJECT")"
	cp -r "$HERE/../../project" "$FELLOW_PROJECT"
fi
CASES=("$@")
if [ ${#CASES[@]} -eq 0 ]; then
	CASES=(pbr:pbr_overview fluid:fluid_cam_01 stress:stress_overview lights:lights_street blend:blend_front
		city:city_aerial foliage:foliage_meadow crowd:crowd_aerial particles:particles_aerial physics:physics_overview
		fire:fire_close burn:burn_top water:water_shore rain:rain_street wind:wind_meadow matching:matching_aerial
		streaming:street_start world:street "rain:rain_street:--weather=storm" "rain:rain_street:--weather=snow")
fi
mkdir -p "$OUT"
printf "renderer\tcase\texit\tseconds\terrors\tunique_errors\tmean\tspread\tportability\n" > "$OUT/summary.tsv"
for r in $RENDERERS; do
	case $r in
	forward_plus) drv=(--rendering-method forward_plus --rendering-driver vulkan) ;;
	mobile) drv=(--rendering-method mobile --rendering-driver vulkan) ;;
	gl_compatibility) drv=(--rendering-method gl_compatibility --rendering-driver opengl3) ;;
	esac
	for c in "${CASES[@]}"; do
		IFS=: read -r scene cam extra <<< "$c"
		name=$scene${extra:+_$(echo "$extra" | tr -c 'a-z0-9\n' '_' | sed 's/^_*//;s/_*$//')}
		d=$OUT/$r/$name
		mkdir -p "$d"
		t0=$(date +%s)
		# shellcheck disable=SC2086
		FELLOW_VULKAN_PROFILE=${PROFILE:-} XDG_DATA_HOME=$OUT/userdata VK_ICD_FILENAMES=$LVP_ICD timeout ${FELLOW_TIMEOUT:-1200} xvfb-run -a -s "-screen 0 1280x720x24" "$FELLOW_BIN" --audio-driver Dummy \
			--path "$FELLOW_PROJECT" "${drv[@]}" --resolution "$RES" --fixed-fps 60 -- --scene="$scene" --mode=capture \
			--cameras="$cam" --settle="$SETTLE" --res="$RES" --out="$d" $extra > "$d.log" 2>&1
		status=$?
		t1=$(date +%s)
		grep -E "ERROR|SCRIPT ERROR|Assertion|Segmentation|signal [0-9]+|Aborted|CrashHandler" "$d.log" \
			| sed -E 's/[0-9]+(\.[0-9]+)?/N/g' | sort | uniq -c | sort -rn > "$d.errors.txt"
		grep "FELLOW_PORTABILITY" "$d.log" | sort -u > "$d.portability.txt"
		portability=$(wc -l < "$d.portability.txt")
		errors=$(grep -c -E "ERROR|SCRIPT ERROR" "$d.log")
		unique=$(wc -l < "$d.errors.txt")
		img=$(find "$d" -name "*.png" | head -1)
		stats=$(python3 "$HERE/image_stats.py" "$img" 2>/dev/null || printf "-\t-")
		printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$r" "$name" "$status" "$((t1 - t0))" "$errors" "$unique" "$stats" "$portability" | tee -a "$OUT/summary.tsv"
	done
done
