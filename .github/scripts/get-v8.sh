set -xe

# clean-up the previously created V8 directories
cd $V8_ROOT
rm -rf v8
rm -rf depot_tools

# clone the V8/Chromium depot tools and add them to path
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
# add depot tools to path
export PATH=$V8_ROOT/depot_tools:$PATH

# fetch v8 and update dependencies
gclient
fetch v8
gclient sync
