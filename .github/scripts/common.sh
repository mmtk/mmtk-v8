set -ex

# The root for the mmtk-v8 repo
export THE_ROOT=`realpath $(dirname "$0")/../..`

# Test with a specific revision.
export V8_VERSION=dd80f2e4cf4fd254c389d6fee58dc8d44d84fed0