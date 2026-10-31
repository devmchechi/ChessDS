#!/bin/sh
# Builds ds/chessds.nds with the BlocksDS Docker image.
# Extra arguments are passed to make, e.g. `./build.sh clean`.
set -e
cd "$(dirname "$0")/ds"
# MSYS_NO_PATHCONV stops Git Bash on Windows from rewriting the paths.
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd):/work" -w /work skylyrac/blocksds:slim-latest make "$@"
