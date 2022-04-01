# MMTk-V8

This repository provides the V8 binding for MMTk.

## Contents

* [Requirements](#requirements)
* [Build](#build)
* [Test](#test)

## Requirements

We maintain an up to date list of the prerequisite for building MMTk and its bindings in the [mmtk-dev-env](https://github.com/mmtk/mmtk-dev-env) repository.
Please make sure your dev machine satisfies those prerequisites.

MMTk/V8 currently only supports `linux-x86_64`.

### Before you continue

The minimal supported Rust version for MMTk-V8 binding is 1.xx.0. Make sure your Rust version is higher than this. We test MMTk-V8
binding with Rust 1.59.0 (as specified in [`rust-toolchain`](mmtk/rust-toolchain)).

### Getting Sources (for MMTk and VM)

First, set environment variables to refer to the root directories for MMTk and V8 respectively (change these to match your preferred locations):

```console
$ export MMTK_V8_ROOT=$HOME/mmtk_v8_root
```

#### MMTk

```console
$ cd $MMTK_V8_ROOT
$ git clone git@github.com:mmtk/mmtk-v8.git
```

#### V8

The following is based on the [V8 documentation](https://v8.dev/docs/source-code).  Please refer to the original documentation if you run into difficulties getting V8.

First, fetch and then update _depot_tools_, which contains the key build dependencies for V8.

```console
$ cd $MMTK_V8_ROOT
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
$ export PATH=`pwd`/depot_tools:$PATH
$ gclient
```

You will need _depot_tools_ in your path when you build V8, so you may wish to update your shell profile (e.g. `.bash_profile`) accordingly.

You should now be able to fetch the V8 sources:

```console
$ fetch v8
```

The fetch command will get the V8 sources and create a build directory for you.   It intentionally puts your V8 repo in a detached head state.

_Tip:_ if you decide to maintain your own fork of V8, be mindful that the fetch command is a necessary step in setting up a new repo; it is not sufficient to simply clone the source.

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
$ cd $MMTK_V8_ROOT/mmtk-v8/mmtk
$ cargo build --features nogc
```

### Building V8

We provide instructions here for building V8 with its [_gm_ workflow](https://v8.dev/docs/build-gn).

First you may wish to create an alias to the _gm_ script, which lives in the `tools/dev` directory of the V8 source tree.

```console
$ alias gm=$MMTK_V8_ROOT/v8/tools/dev/gm.py
```

You may wish to add the above alias to your shell profile.

Now you can build V8.

### First Ensure you can Build Release V8

Before trying to build V8 with MMTk, ensure you can build V8 without MMTk:

```console
$ cd $MMTK_V8_ROOT/v8
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

Create a gn config for building v8 with mmtk, which we'll call `x64.debug-mmtk`.

Use `gn`, which will open an editor:

```console
$ cd $MMTK_V8_ROOT/v8
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
v8_disable_write_barriers = true
v8_enable_single_generation = true
v8_enable_shared_ro_heap = false
v8_enable_pointer_compression = false
v8_enable_third_party_heap = true
v8_third_party_heap_files = [ "../mmtk-v8/v8/third_party/heap/mmtk/mmtk.cc", 
"../mmtk-v8/v8/third_party/heap/mmtk/mmtk.h",
"../mmtk-v8/v8/third_party/heap/mmtk/mmtkUpcalls.h",
"../mmtk-v8/v8/third_party/heap/mmtk/mmtkUpcalls.cc"]
v8_third_party_heap_libs = [ "../mmtk-v8/mmtk/target/debug/libmmtk_v8.so" ]
```

Then build:

```console
$ gm x64.debug-mmtk
```

## Test

The [V8 document on testing](https://v8.dev/docs/test) discusses various options for running V8 tests.

For instance, `benchmarks` tests can be run on _"V8 with MMTk"_ as:

```console
$ gm x64.debug-mmtk benchmarks/*
# autoninja -C out/x64.debug-mmtk d8
ninja: Entering directory `out/x64.debug-mmtk'
ninja: no work to do.
# "/usr/bin/python2" tools/run-tests.py --outdir=out/x64.debug-mmtk benchmarks/*
Build found: /home/javad/sources/v8/v8/v8/out/x64.debug-mmtk
>>> Autodetected:
verify_csa
>>> Running tests for x64.debug
>>> Running with test processors
[02:20|%  96|+  53|-   0]: Done
>>> 55 base tests produced 53 (96%) non-filtered tests
>>> 53 tests ran
Done! - V8 compilation finished successfully.
```

### Limitations

There are a few reasons why MMTk-V8 can not pass all V8 tests, including regression and unit tests:

* MMTk-V8 does not support garbage collection yet.
* MMTk does not support multiple heap instances yet.
* Many of the V8 tests are unit tests targeting specific V8 features, and are not applicable to MMTk.
