#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=${DOCKER_IMAGE:-nox-modernized-builder:local}

docker build --file "$ROOT/Dockerfile" --tag "$IMAGE" "$ROOT"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$ROOT:/workspace" \
    --workdir /workspace \
    "$IMAGE" \
    sh -ec 'sh src/build.sh && sh src/display/build.sh && sh src/launcher/build.sh'