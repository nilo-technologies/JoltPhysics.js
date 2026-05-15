#!/bin/sh
# Nilo fork publish script. Differs from upstream `ci/npm-publish.sh`:
#   - Reads name + registry from package.json (publishConfig.registry); upstream
#     hardcodes `jolt-physics` and the public npm registry.
#   - Adds `--tag nilo` for prerelease versions (X.Y.Z-nilo.N) so the published
#     version doesn't claim the `latest` dist-tag on the registry.
#   - `set -e` so a publish failure aborts the workflow.
#
# Kept as a separate script (rather than modifying `ci/npm-publish.sh`) so upstream
# merges into this fork are conflict-free. The fork's workflow calls this; upstream's
# workflow continues to call upstream's script (and harmlessly fails on this fork due
# to missing NPM_TOKEN -- disable upstream's workflow in repo Settings).

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
