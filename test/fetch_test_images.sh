#!/usr/bin/env bash

# https://stackoverflow.com/a/246128
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"

if ! [[ -f "${SCRIPT_DIR}/resources/qoi_test_images.zip" ]]; then
    echo "$(basename $0): test files does not exists, fetching them..."

    wget -P "${SCRIPT_DIR}/resources/" --no-verbose --force-progress https://qoiformat.org/qoi_test_images.zip
    cd "${SCRIPT_DIR}/resources/"
    unzip qoi_test_images.zip
else
    echo "$(basename $0): test files already exists"
fi
