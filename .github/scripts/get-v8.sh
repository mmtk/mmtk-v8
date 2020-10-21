set -xe

# create and change to the V8 directory
cd $THE_ROOT
rm -R -f v8
mkdir v8
cd v8

# clone the V8/Chromium depot tools and add them to path
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
# add depot tools to path
export PATH=$V8_ROOT/depot_tools:$PATH

# fetch v8 and update dependencies
gclient
fetch v8
gclient sync
