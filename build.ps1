# Builds ds/chessds.nds with the BlocksDS Docker image (needs Docker Desktop).
# Extra arguments are passed to make, e.g. `./build.ps1 clean`.
$ErrorActionPreference = "Stop"
$ds = Join-Path $PSScriptRoot "ds"
docker run --rm -v "${ds}:/work" -w /work skylyrac/blocksds:slim-latest make @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
