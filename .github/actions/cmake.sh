#!/usr/bin/env bash

set -x

echo "Starting CMake Entry"
echo "HOME:" $HOME
echo "GITHUB_WORKSPACE:" $GITHUB_WORKSPACE
echo "GITHUB_EVENT_PATH:" $GITHUB_EVENT_PATH
echo "PWD:" `pwd`

CXX_COMPILER=${COMPILER/clang/clang++}
CXX_COMPILER=${CXX_COMPILER/gcc/g++}

cd ${GITHUB_WORKSPACE}
mkdir $SIMDB_BUILD_TYPE
cd $SIMDB_BUILD_TYPE

# Use ccache as the compiler launcher when available (no-op if ccache is absent)
CCACHE_FLAG=""
if command -v ccache >/dev/null 2>&1; then
    CCACHE_FLAG="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

cmake .. -DCMAKE_C_COMPILER=$COMPILER -DCMAKE_CXX_COMPILER=$CXX_COMPILER -DCMAKE_BUILD_TYPE=$SIMDB_BUILD_TYPE $CCACHE_FLAG
if [ $? -ne 0 ]; then
    echo "ERROR: CMake for SimDB failed"
    exit 1
fi
