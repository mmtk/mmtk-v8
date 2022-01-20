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
git -C v8 checkout $V8_VERSION

gclient sync
# Set depot_tools to the given revision.
# This needs to be done after gclient sync. Otherwise gclient sync will reset depot_tools to HEAD
git -C depot_tools checkout $DEPOT_TOOLS_VERSION