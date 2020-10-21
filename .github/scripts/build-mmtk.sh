set -xe

# simply build mmtk-v8 with nogc
cd $THE_ROOT/mmtk-v8/mmtk
rustup run $RUSTUP_TOOLCHAIN cargo build --features nogc
