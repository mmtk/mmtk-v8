set -xe

. $(dirname "$0")/common.sh

# simply build mmtk-v8 with nogc
rustup toolchain install $RUSTUP_TOOLCHAIN --target x86_64-unknown-linux-gnu --component clippy rustfmt
rustup override set $RUSTUP_TOOLCHAIN

cd $THE_ROOT/mmtk && cargo build --features nogc
