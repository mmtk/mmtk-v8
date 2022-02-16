set -xe

# change to the V8 directory
cd $V8_ROOT/v8

# add V8 depot tools to path
export PATH=$V8_ROOT/depot_tools:$PATH

# generate the default setup and copy mmtk-v8's required arguments
mkdir -p out/x64.optdebug-mmtk
# copy the args
cp $BINDING_PATH/.github/scripts/args-optdebug.gn out/x64.optdebug-mmtk/args.gn
# generate files according to the copied args file
gn gen out/x64.optdebug-mmtk
# build our V8 setup, named optdebug-mmtk
./tools/dev/gm.py x64.optdebug-mmtk.all
