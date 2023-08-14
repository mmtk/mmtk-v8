set -xe

. $(dirname "$0")/common.sh

sudo apt-get install -y build-essential curl git python2 python3 pkg-config sudo vim clang
sudo apt-get clean
