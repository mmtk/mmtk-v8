set -xe

. $(dirname "$0")/common.sh

export PATH=~/.cargo/bin:$PATH
# Check if we have cross available
if ! [ -x "$(command -v cross)" ]; then
  echo 'Cross is not available. Will install cross.' >&2
  # lock to version 0.2.4. This version uses libc 2.31 for x86_64.
  # This matches the version in the sysroot used by V8 (debian bullseye)
  cargo install cross@0.2.4 --git https://github.com/cross-rs/cross
fi

# simply build mmtk-v8 with nogc
cd $THE_ROOT/mmtk
cross build --target x86_64-unknown-linux-gnu --features nogc
