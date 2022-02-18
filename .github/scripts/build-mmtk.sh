set -xe

. $(dirname "$0")/common.sh

# simply build mmtk-v8 with nogc
cd $THE_ROOT/mmtk
rustup run $RUSTUP_TOOLCHAIN cargo build --features nogc
