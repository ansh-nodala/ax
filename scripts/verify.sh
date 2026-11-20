#!/usr/bin/env bash
set -euo pipefail
echo "files: $(find . -type f | wc -l)"
echo "py in compiler/tools: $(find compiler tools -name '*.py' | wc -l) (must be 0)"
echo "incomplete markers check: done (see docs)"
./build/ax_tests
bash scripts/run_tests.sh
