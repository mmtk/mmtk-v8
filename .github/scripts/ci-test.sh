set -xe

CUR=$(dirname "$0")

$CUR/ci-setup.sh
$CUR/build-mmtk.sh
$CUR/get-v8.sh
$CUR/build-v8.sh
# $CUR/run-regress.sh
# $CUR/run-unittests.sh
$CUR/run-benchmarks.sh
