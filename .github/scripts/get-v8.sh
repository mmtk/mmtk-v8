set -xe

. $(dirname "$0")/common.sh

# clean-up the previously created V8 directories
mkdir -p $V8_ROOT
cd $V8_ROOT
rm -rf v8
rm -rf depot_tools
rm -f .gclient
rm -f .gclient_entries

# clone the V8/Chromium depot tools and add them to path
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
# add depot tools to path
export PATH=$V8_ROOT/depot_tools:$PATH

# fetch v8 and update dependencies
gclient
fetch v8
# Test with a specific revision.
# TODO: We should have a better way of specifying version.
git -C v8 checkout $V8_VERSION

gclient sync