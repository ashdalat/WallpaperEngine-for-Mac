#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not found" >&2
    exit 1
fi

mapfile -d '' files < <(
    git ls-files -z -- \
        '*.c' '*.cc' '*.cpp' '*.cxx' \
        '*.h' '*.hh' '*.hpp' '*.hxx' \
        '*.ixx' '*.cppm'
)

if ((${#files[@]} > 0)); then
    clang-format -i "${files[@]}"
fi
