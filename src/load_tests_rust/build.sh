#!/bin/bash

# USING:
#./build.sh build_dir

export CARGO_TARGET_DIR=$1/src/load_tests_rust
export LIBAKTUALIZR_C_PATH=$1/src/libaktualizr-c
cargo build -vv
