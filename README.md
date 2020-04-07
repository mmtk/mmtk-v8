# mmtk-v8

This repository provides the V8 binding for MMTk.

## Contents
* [Requirements](#requirements)
* [Build](#build)
* [Test](#test)

## Requirements

This sections describes prerequisite for building V8 with MMTk.

### Before You Start

To use MMTk with V8, you will need to build MMTk with the V8 bindings, and you'll need to build V8 with the MMTk bindings.

#### Software Dependencies

* V8 comes with clear [instructions](https://v8.dev/docs/source-code) which you should refer to as necessary.
* MMTk requires the rustup nightly toolchain
  * Please visit [rustup.rs](https://rustup.rs/) for installation instructions.

#### Supported Hardware

MMTk/V8 currently only supports `linux-x86_64`.

_Tested on a Ryzen 9 3900X Machine with 32GB RAM, running Ubuntu 18.04-amd64 (Linux kernel version 4.15.0-21-generic)._

### Getting Sources (for MMTk and VM)

First environment variables to refer to the root directories for MMTk and V8 respectively (change these to match your preferred locations):

```console
$ export MMTK_ROOT=$HOME/mmtk
$ export V8_ROOT=$HOME/v8
```

#### MMTk

```console
$ cd $MMTK_ROOT
$ git clone git@github.com:mmtk/mmtk-v8.git
$ git clone git@github.com:mmtk/mmtk-core.git
```

#### V8

The following is based on the [V8 documentation](https://v8.dev/docs/source-code).  Please refer to the original documentation if you run into difficulties getting V8.

First, fetch and then update _depot_tools_, which contains the key build dependencies for V8.

```console
$ cd $V8_ROOT
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
$ export PATH=`pwd`/depot_tools:$PATH
$ gclient
```

You will need _depot_tools_ in your path when you build V8, so you may wish to update your shell profile (e.g. `.bash_profile`) accordingly.

You should now be able to fetch the V8 sources:

```console
$ fetch v8
```

The fetch command will get the V8 sources and create a build directory for you.   It intentionally puts your V8 repo in a detached head state.  Tip: if you decide to maintain your own fork of V8, be mindful that the fetch command is a necessary step in setting up a new repo; it is not sufficient to simply clone the source.

To update your V8 sources, use:

```console
$ fetch v8
```

Occasionally V8 won't build after a fetch because dependencies have changed.   This is fixed by synching:

```console
$ gclient sync
```

## Build

First build MMTk, then V8

### Building MMTk

```console
$ cd $MMTK_ROOT/mmtk-v8/mmtk
$ cargo +nightly build --features nogc
```

_**Note:** You may need to use ssh-agent before attempting the cargo build in order to authenticate with github (see [here](https://github.com/rust-lang/cargo/issues/3487) for more info):_

```console
$ eval `ssh-agent`
$ ssh-add
```


### Building V8

We provide instructions here for building V8 with its [_gm_ workflow](https://v8.dev/docs/build-gn).

First you may wish to create an alias to the _gm_ script, which lives in the `tools/dev` directory of the V8 source tree.

```console
$ alias gm=$V8_ROOT/v8/tools/dev/gm.py
```

You may wish to add the above alias to your shell profile.

Now you can build V8.

### First Ensure you can Build Release V8

Before trying to build V8 with MMTk, ensure you can build V8 without MMTk:

```console
$ cd $V8_ROOT/v8
$ gm x64.release
```
The above builds a standard release build of v8.

### Next Ensure you can Build a Debug V8

You need to create a config file, which we'll call `x64.debug`.

Use `gn`, which will open an editor:

```console
$ gn args out/x64.debug
```

Enter the following values:

```
is_component_build = true
is_debug = true
symbol_level = 2
target_cpu = "x64"
use_goma = false
v8_enable_backtrace = true
v8_enable_fast_mksnapshot = true
v8_enable_verify_csa = true
v8_enable_slow_dchecks = false
v8_optimized_debug = false
```


### Then Build with MMTk

First, build MMTk:

```console
$ cd $MMTK_ROOT/mmtk-v8/mmtk
$ cargo +nightly build --features nogc
```

Then create a gn config for building v8 with mmtk, which we'll call `x64.debug-mmtk`.

Use `gn`, which will open an editor:

```console
$ cd $V8_ROOT/v8
$ gn args out/x64.debug-mmtk
```

Enter the following values:

```
is_component_build = true
is_debug = true
symbol_level = 2
target_cpu = "x64"
use_goma = false
v8_enable_backtrace = true
v8_enable_fast_mksnapshot = true
v8_enable_verify_csa = true
v8_enable_slow_dchecks = false
v8_optimized_debug = false
v8_enable_third_party_heap = true
v8_third_party_heap_files = [ "<path to mmtk-v8>/v8/third_party/heap/mmtk/mmtk.cc", "<path to mmtk-v8>/v8/third_party/heap/mmtk/mmtk.h" ]
v8_third_party_heap_libs = [ "<path to mmtk-v8>/mmtk/target/debug/libmmtk_v8.so" ]
```

Then build:

```console
$ gm x64.debug-mmtk
```
## Test


