#!/bin/sh

set -e

VERSION=$(node -pe "require('./package.json').version")
NAME=$(node -pe "require('./package.json').name")
REGISTRY=$(node -pe "require('./package.json').publishConfig?.registry || 'https://registry.npmjs.org'")

if npm view "${NAME}@${VERSION}" version --registry="$REGISTRY" >/dev/null 2>&1; then
  echo "Version $VERSION of $NAME is already published at $REGISTRY, nothing to do."
else
  echo "Publishing $NAME@$VERSION to $REGISTRY..."
  # npm requires an explicit dist-tag for prerelease versions (e.g. 5.5.0-nilo.0).
  TAG_ARGS=""
  case "$VERSION" in
    *-*)
      TAG_ARGS="--tag nilo"
      echo "Using dist-tag 'nilo' (install e.g. @nilo-technologies/jolt-physics@nilo)."
      ;;
  esac
  npm publish --registry="$REGISTRY" $TAG_ARGS
  echo "Published $NAME@$VERSION."
fi

