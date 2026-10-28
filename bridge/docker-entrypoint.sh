#!/usr/bin/env bash
set -euo pipefail

mkdir -p "${STATE_DIR:-/app/data}" "${PROVISION_OUT_DIR:-/app/provision-out}"

# Se existir um estado de chaves geradas pelo `npm run provision` em /app/data/sentinel_keys.env, carrega-o
if [ -f "${STATE_DIR:-/app/data}/sentinel_keys.env" ]; then
  set -a
  # shellcheck disable=SC1090
  source "${STATE_DIR:-/app/data}/sentinel_keys.env"
  set +a
fi

exec "$@"

