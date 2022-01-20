set -ex

# The root for the mmtk-v8 repo
export THE_ROOT=`realpath $(dirname "$0")/../..`

# Test with a specific revision.
export V8_VERSION=191b637f28c0e2c6ca5f2d6ac89377039a754337
# The commit after this requires python 3.7. This is the only reason we fix to this revision.
export DEPOT_TOOLS_VERSION=b674278ce71b2ee683b8b0c98c9a64152988ecdb
