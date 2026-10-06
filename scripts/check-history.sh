#!/bin/sh
set -eu
target=$(git rev-parse --verify "${1:-HEAD}^{commit}")
root=$(git rev-list --max-parents=0 "$target" | tail -n 1)
# --from excludes its starting commit, so explicitly validate that root as well.
git show -s --format=%B "$root" | npx --no-install commitlint
if test "$root" != "$target"; then
  npx --no-install commitlint --from "$root" --to "$target"
fi
