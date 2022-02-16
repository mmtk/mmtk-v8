set -xe

export RUSTFLAGS="-D warnings"

cd $BINDING_PATH/mmtk
cargo clippy
cargo clippy --release
cargo fmt -- --check