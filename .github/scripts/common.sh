set -ex

# The root for the mmtk-v8 repo
export THE_ROOT=`realpath $(dirname "$0")/../..`
export V8_ROOT=$THE_ROOT/deps

# Test with a specific revision.
export V8_VERSION=`cargo read-manifest --manifest-path=$THE_ROOT/mmtk/Cargo.toml | python3 -c 'import json,sys; print(json.load(sys.stdin)["metadata"]["v8"]["v8_version"])'`
# The commit after this requires python 3.7. This is the only reason we fix to this revision.
export DEPOT_TOOLS_VERSION=`cargo read-manifest --manifest-path=$THE_ROOT/mmtk/Cargo.toml | python3 -c 'import json,sys; print(json.load(sys.stdin)["metadata"]["v8"]["depot_tools_version"])'`

export RUSTUP_TOOLCHAIN=`cat $THE_ROOT/mmtk/rust-toolchain`
