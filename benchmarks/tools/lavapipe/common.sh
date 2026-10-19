#!/bin/bash
# Shared settings of the lavapipe scripts (Linux only). Override with the environment:
#   FELLOW_BIN      the FellowEngine template_release binary (linuxbsd)
#   FELLOW_PROJECT  a copy of benchmarks/project (one per script running at the same time: each writes override.cfg)
#   RES             resolution (480x270)
FELLOW_BIN=${FELLOW_BIN:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/bin/godot.linuxbsd.template_release.x86_64}
FELLOW_PROJECT=${FELLOW_PROJECT:-/tmp/fellow_lvp_project}
RES=${RES:-480x270}
LVP_ICD=${LVP_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
if [ ! -d "$FELLOW_PROJECT" ]; then
	cp -r "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/project" "$FELLOW_PROJECT"
fi
# fellow_run <outdir> "<override lines under [rendering]>" "<environment>" <bench args...>
fellow_run() {
	local out=$1 ov=$2 envs=$3
	shift 3
	mkdir -p "$out"
	printf "[rendering]\n%s\n" "$ov" > "$FELLOW_PROJECT/override.cfg"
	env $envs VK_ICD_FILENAMES=$LVP_ICD timeout 5400 xvfb-run -a "$FELLOW_BIN" --path "$FELLOW_PROJECT" --rendering-driver vulkan --resolution $RES --fixed-fps 60 -- --res=$RES --out="$out" "$@" > "$out.log" 2>&1
	rm -f "$FELLOW_PROJECT/override.cfg"
}
# For exactness checks: no undefined derivatives after discard (decision 0057's demote diagnostics).
DEMOTE="fellow/debug/scene_shader_experiments=4194304"
