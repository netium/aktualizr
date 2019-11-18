#!/bin/bash

# USING:
#./check_updates.sh build_dir meta_dir

export CARGO_TARGET_DIR=$1/src/load_tests_rust
export LD_LIBRARY_PATH=$1/src/libaktualizr-c
cargo run load-tests-rust $2
