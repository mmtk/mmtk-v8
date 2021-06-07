set -xe

. $(dirname "$0")/common.sh

# change to the V8 directory
cd $V8_ROOT/v8

# add V8 depot tools to path
export PATH=$V8_ROOT/depot_tools:$PATH

# run tests
tools/dev/gm.py x64.debug-mmtk.checkall
