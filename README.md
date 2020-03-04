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

* V8 comes with clear [instructions](https://v8.dev/docs/source-code) which we step through below.
* MMTk requires the rustup nightly toolchain
  * Please visit [rustup.rs](https://rustup.rs/) for installation instructions.

#### Supported Hardware

MMTk/V8 currently only supports `linux-x86_64`.

_Tested on a Ryzen 9 3900X Machine with 32GB RAM, running Ubuntu 18.04-amd64 (Linux kernel version 4.15.0-21-generic)._

### Getting Sources (for MMTk and VM)


#### MMTk

```console
$ git clone git@gitlab.anu.edu.au:mmtk/mmtk-v8.git
$ git clone git@gitlab.anu.edu.au:mmtk/mmtk-core.git
```

#### V8

Please refer to the [V8 documentation](https://v8.dev/docs/source-code) for more information.

First, fetch and then update _depot_tools_, which contains the key build dependencies for V8.

```console
$ git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
$ export PATH=`pwd`/depot_tools:$PATH
$ gclient
```

Then fetch the V8 sources:

```console
$ fetch v8
```

This will get the V8 sources and create a build directory for you.   It intentionally puts your V8 repo in a detached head state.  

To update your V8 sources, use:

```console
$ fetch v8
```

## Build

First build MMTk, then V8

### Building MMTk


_**Note:** MMTk is only tested with the `server` build variant._

### Building V8


## Test


