set -xe

# change to the V8 directory
cd $V8_ROOT/v8

# add V8 depot tools to path
export PATH=$V8_ROOT/depot_tools:$PATH

# generate the default setup and copy mmtk-v8's required arguments
mkdir out/x64.debug-mmtk
# copy the args
cp $THE_ROOT/mmtk-v8/.github/scripts/args.gn out/x64.debug-mmtk/
# generate files according to the copied args file
gn gen out/x64.debug-mmtk
# build our V8 setup, named debug-mmtk
./tools/dev/gm.py x64.debug-mmtk
