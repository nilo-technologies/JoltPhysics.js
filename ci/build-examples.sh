#!/bin/bash

set -e

npm install

bash ./ci/install-emsdk.sh
source ./emsdk/emsdk_env.sh

if [ -n "${NILO_CI_JOLT_PHYSICS_PATH:-}" ] && [ -f "${NILO_CI_JOLT_PHYSICS_PATH}/Build/CMakeLists.txt" ]; then
  echo "Building with local Jolt C++ tree: ${NILO_CI_JOLT_PHYSICS_PATH}"
  sh ./build.sh Distribution "-DJOLT_PHYSICS_PATH=${NILO_CI_JOLT_PHYSICS_PATH}"
else
  npm run build
fi
