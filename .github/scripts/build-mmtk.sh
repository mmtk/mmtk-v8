set -xe

# simply build mmtk-v8 with nogc
cd $BINDING_PATH/mmtk
rustup run $RUSTUP_TOOLCHAIN cargo build --features nogc
