set -xe

. $(dirname "$0")/common.sh

sudo apt-get install build-essential software-properties-common -y
sudo add-apt-repository ppa:deadsnakes/ppa
sudo apt-get install -y build-essential curl git python2 python3.9 pkg-config sudo vim clang
sudo apt-get clean
# This is a hack to make sure that we're using python 3.9
# The default python 3.10 will not work with v8 build scripts
# TODO: Remove this after moving v8 commit hash forward
sudo ln -sf /usr/bin/python3.9 /usr/bin/python3