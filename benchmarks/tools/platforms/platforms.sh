#!/bin/bash
# FellowEngine platform checks (milestone 49): Godot's export templates for other platforms, built and run in docker
# containers on one x86_64 Linux machine (no other hardware needed). See README.md.
#   platforms.sh image <cross|wine|arm64|android|web>  build a container image (fellow-platforms-<name>)
#   platforms.sh build <windows|linux-arm64|android|web> [scons args]
#   platforms.sh shell <image> [command]                a command in a container, the build tree mounted at /src
#                                                       (and FELLOW_PLATFORMS_MOUNTS="host:container ...")
# The build tree is a copy of the repository (rsync, without .git and bin/obj of the host), $FELLOW_PLATFORMS_TREE
# (/tmp/fellow_platforms/src): the containers' SCons is not the host's, so they keep their own .sconsign and objects.
# Behind an HTTPS proxy with its own CA, set FELLOW_EXTRA_CA to that CA's bundle: it is added to the images (local
# images only).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
TREE=${FELLOW_PLATFORMS_TREE:-/tmp/fellow_platforms/src}
JOBS=${JOBS:-$(nproc)}
NET=()
PROXY_ARGS=()
if [ -n "${HTTPS_PROXY:-}" ]; then
	NET=(--network host)
	PROXY_ARGS=(--build-arg "HTTPS_PROXY=$HTTPS_PROXY" --build-arg "https_proxy=$HTTPS_PROXY")
fi

sync_tree() {
	mkdir -p "$TREE"
	# Generated sources (*.gen.*) are the container's: the host's (another platform's modules_enabled.gen.h, say)
	# would replace them with an older time stamp, and SCons does not regenerate a target that changed (milestone 50:
	# a web link with Linux's modules).
	rsync -a --delete --exclude=/.git --exclude=/bin --exclude='/.sconsign*' --exclude='*.o' --exclude='*.a' \
		--exclude='*.gen.*' --exclude=/benchmarks/project/external --exclude=/benchmarks/project/.godot "$REPO/" "$TREE/"
}

run_in() {
	local image=$1
	shift
	local mounts=(-v "$TREE:/src")
	for m in ${FELLOW_PLATFORMS_MOUNTS:-}; do
		mounts+=(-v "$m")
	done
	docker run --rm "${NET[@]}" "${mounts[@]}" -w /src "fellow-platforms-$image" bash -c "$*"
}

case "${1:-}" in
image)
	ctx=$(mktemp -d)
	mkdir -p "$ctx/certs"
	if [ -n "${FELLOW_EXTRA_CA:-}" ]; then cp "$FELLOW_EXTRA_CA" "$ctx/certs/extra.crt"; fi
	cp "$HERE/Dockerfile.$2" "$ctx/Dockerfile"
	docker build "${NET[@]}" "${PROXY_ARGS[@]}" -t "fellow-platforms-$2" "$ctx"
	rm -rf "${ctx:?}"
	;;
build)
	target=$2
	shift 2
	sync_tree
	common="target=template_release disable_path_overrides=no -j$JOBS"
	case "$target" in
	windows) run_in cross "scons platform=windows use_mingw=yes arch=x86_64 d3d12=no $common $*" ;;
	linux-arm64) run_in cross "PATH=/opt/arm64-bin:\$PATH scons platform=linuxbsd arch=arm64 CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ LINK=aarch64-linux-gnu-g++ $common $*" ;;
	android) run_in android "scons platform=android arch=arm64 $common $*" ;;
	web) run_in web "scons platform=web $common $*" ;;
	*) echo "unknown target $target" >&2; exit 2 ;;
	esac
	;;
shell)
	image=$2
	shift 2
	run_in "$image" "${*:-bash}"
	;;
*)
	sed -n '2,11p' "$0"
	exit 2
	;;
esac
