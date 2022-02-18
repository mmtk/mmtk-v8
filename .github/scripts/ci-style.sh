set -xe

export RUSTFLAGS="-D warnings"

cd $THE_ROOT/mmtk
cargo clippy
cargo clippy --release
cargo fmt -- --check
