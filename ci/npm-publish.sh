#!/bin/sh

set -e

VERSION=$(node -pe "require('./package.json').version")
NAME=$(node -pe "require('./package.json').name")
REGISTRY=$(node -pe "require('./package.json').publishConfig?.registry || 'https://registry.npmjs.org'")

if npm view "${NAME}@${VERSION}" version --registry="$REGISTRY" >/dev/null 2>&1; then
  echo "Version $VERSION of $NAME is already published at $REGISTRY, nothing to do."
else
  echo "Publishing $NAME@$VERSION to $REGISTRY..."
  npm publish --registry="$REGISTRY"
  echo "Published $NAME@$VERSION."
fi

