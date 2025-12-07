#!/usr/bin/env bash
set -euo pipefail

PYTHON_BIN=${PYTHON_BIN:-python3}

if ! $PYTHON_BIN -c "import pybind11" 2>/dev/null; then
  echo "pybind11 not found in $PYTHON_BIN environment. Install with:"
  echo "$PYTHON_BIN -m pip install pybind11"
  exit 1
fi

mkdir build && cd build
cmake .. -Dpybind11_DIR=$(python3 -m pybind11 --cmakedir) \
 -DCMAKE_C_COMPILER=gcc \
 -DCMAKE_CXX_COMPILER=g++

make
cd ..
