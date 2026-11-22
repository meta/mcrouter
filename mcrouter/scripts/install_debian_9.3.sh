#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get update

sudo apt-get install -y \
    autoconf \
    autoconf-archive \
    automake \
    binutils-dev \
    cmake \
    g++ \
    gcc \
    git \
    googletest \
    libboost-all-dev \
    libcap-dev \
    libdouble-conversion-dev \
    libdwarf-dev \
    libelf-dev \
    libevent-dev \
    libgflags-dev \
    libgoogle-glog-dev \
    libgtest-dev \
    libiberty-dev \
    libjemalloc-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    libssl-dev \
    libssl1.0.2 \
    libtool \
    libunwind8-dev \
    make \
    pkg-config \
    ragel \
    scons \
    sudo \
    unzip \
    zlib1g-dev

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh debian-9.3 "$@"
