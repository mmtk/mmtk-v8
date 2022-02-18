set -xe

. $(dirname "$0")/common.sh

CUR=$(dirname "$0")

$CUR/build-mmtk.sh
$CUR/get-v8.sh
$CUR/build-v8.sh
$CUR/test-v8.sh
