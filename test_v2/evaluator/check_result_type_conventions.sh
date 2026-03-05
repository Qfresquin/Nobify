#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

check_forbidden_pattern() {
    local label="$1"
    local pattern="$2"
    local path="$3"
    local grep_opts="$4"

    local matches=""
    matches=$(grep ${grep_opts} -E "$pattern" "$path" || true)
    if [[ -n "$matches" ]]; then
        echo "[FAIL] ${label}"
        echo "$matches"
        exit 1
    fi
}

check_forbidden_pattern \
    "forbidden return !eval_should_stop(ctx) pattern" \
    'return[[:space:]]+!eval_should_stop\(ctx\);' \
    src_v2/evaluator \
    '-RIn --include=*.c --include=*.h'

check_forbidden_pattern \
    "forbidden bool eval_handle_* signatures" \
    '^bool[[:space:]]+eval_handle_' \
    src_v2/evaluator \
    '-RIn --include=*.c --include=*.h'

check_forbidden_pattern \
    "legacy bool Eval_Native_Command_Handler signature" \
    'typedef[[:space:]]+bool[[:space:]]*\(\*[[:space:]]*Eval_Native_Command_Handler[[:space:]]*\)' \
    src_v2/evaluator/evaluator.h \
    '-In'

state_write_matches=$(grep -RIn --include='*.c' --include='*.h' -E 'ctx->(oom|stop_requested)[[:space:]]*=' src_v2/evaluator || true)
if [[ -n "$state_write_matches" ]]; then
    unauthorized=$(printf '%s\n' "$state_write_matches" | grep -v '^src_v2/evaluator/evaluator.c:' || true)
    if [[ -n "$unauthorized" ]]; then
        echo "[FAIL] direct writes to ctx->oom/ctx->stop_requested outside evaluator.c"
        echo "$unauthorized"
        exit 1
    fi
fi

echo "[OK] result type conventions checks passed"
