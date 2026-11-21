#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$script_dir/../.." && pwd)
image_name=${DDRMINI_DOCKER_IMAGE:-itgmania-ddrmini-cross:bullseye}

docker build \
  --file "$script_dir/Dockerfile" \
  --tag "$image_name" \
  "$script_dir"

docker run --rm \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp \
  --env DDRMINI_BUILD_JOBS="${DDRMINI_BUILD_JOBS:-2}" \
  --volume "$repo:/work" \
  --workdir /work \
  "$image_name" \
  /work/Tools/ddrmini/build-itgmania.sh

