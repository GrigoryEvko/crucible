#!/usr/bin/env bash
# A planted script that runs the help output of the build tool to find the targets of a build.
set -euo pipefail
cmake --build "$1" --target help
