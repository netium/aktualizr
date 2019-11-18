#!/bin/bash

# USING:
#./provisioning.sh build_dir meta_path creds_path devices_num gateway

export CARGO_TARGET_DIR=$1/src/load_tests_rust
export LD_LIBRARY_PATH=$1/src/libaktualizr-c
cargo run load-tests-rust $2 $3 $4 $5
