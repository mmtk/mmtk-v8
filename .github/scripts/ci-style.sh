set -xe

. $(dirname "$0")/common.sh

export RUSTFLAGS="-D warnings"

cd $THE_ROOT/mmtk
cargo clippy
cargo clippy --release
cargo fmt -- --check
