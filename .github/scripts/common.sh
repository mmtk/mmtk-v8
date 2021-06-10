set -ex

# The root for the mmtk-v8 repo
export THE_ROOT=`realpath $(dirname "$0")/../..`

# Test with a specific revision.
export V8_VERSION=191b637f28c0e2c6ca5f2d6ac89377039a754337