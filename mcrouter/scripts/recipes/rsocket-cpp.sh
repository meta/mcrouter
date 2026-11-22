#!/usr/bin/env bash

source common.sh

if [[ ! -d rsocket-cpp ]]; then
  git clone https://github.com/rsocket/rsocket-cpp.git
  cd "$PKG_DIR/rsocket-cpp" || die "cd fail"
  if [[ -f "$REPO_BASE_DIR/mcrouter/RSOCKETCPP_COMMIT" ]]; then
    RSOCKETCPP_COMMIT="$(head -n 1 "$REPO_BASE_DIR/mcrouter/RSOCKETCPP_COMMIT")"
    echo "RSOCKETCPP_COMMIT file found: using rsocket-cpp commit $RSOCKETCPP_COMMIT"
    git checkout "$RSOCKETCPP_COMMIT"
  else
    echo "No RSOCKETCPP_COMMIT file, using rsocket-cpp HEAD=$(git rev-parse HEAD)"
  fi
fi

cd "$PKG_DIR/rsocket-cpp/build" || die "cd fail"
rm -f ../CMakeCache.txt

cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_TESTS=OFF
make $MAKE_ARGS gmock
make $MAKE_ARGS && make install $MAKE_ARGS
