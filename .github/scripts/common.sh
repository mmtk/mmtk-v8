set -ex

# The root for the mmtk-v8 repo
export THE_ROOT=`realpath $(dirname "$0")/../..`
export V8_ROOT=$THE_ROOT/deps

# Test with a specific revision.
export V8_VERSION=`sed -n 's/^v8_version.=."\(.*\)"$/\1/p' < $THE_ROOT/mmtk/Cargo.toml`
# The commit after this requires python 3.7. This is the only reason we fix to this revision.
export DEPOT_TOOLS_VERSION=`sed -n 's/^depot_tools_version.=."\(.*\)"$/\1/p' < $THE_ROOT/mmtk/Cargo.toml`

export RUSTUP_TOOLCHAIN=`cat $THE_ROOT/mmtk/rust-toolchain`
