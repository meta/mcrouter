#!/usr/bin/env bash
set -o errexit


function run_mcrouter() {
        local CONFIG_PATH=$1
        mcrouter \
        --config file:${CONFIG_PATH} \
        -p "${MCROUTER_LISTEN_PORT:-5000}" \
	"$@"
}


if [ -z "${MCROUTER_CONFIG_PATH}" ]; then
  echo "MCROUTER_CONFIG_PATH undefined! Using bundled default ..."
  export MCROUTER_CONFIG_PATH=/etc/mcrouter/mcrouter.json
fi

run_mcrouter "${MCROUTER_CONFIG_PATH}" "$@"
