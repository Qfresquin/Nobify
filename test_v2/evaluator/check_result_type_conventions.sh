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

system_matches=$(grep -RIn --include='*.c' --include='*.h' -E '\bsystem[[:space:]]*\(' src_v2 test_v2 || true)
if [[ -n "$system_matches" ]]; then
    unauthorized=$(printf '%s\n' "$system_matches" | grep -v '^test_v2/evaluator/test_evaluator_v2_common.h:' || true)
    if [[ -n "$unauthorized" ]]; then
        echo "[FAIL] system(...) usage outside the approved test allowlist"
        echo "$unauthorized"
        exit 1
    fi
fi

legacy_temp_path_matches=$(grep -RIn --include='*.c' --include='*.h' -E 'Temp_tests/(work|bin|obj)([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])TEMP_TESTS_(WORK|BIN|OBJ)($|[^A-Za-z0-9_])' src_v2 test_v2 || true)
if [[ -n "$legacy_temp_path_matches" ]]; then
    echo "[FAIL] legacy fixed Temp_tests work/bin/obj paths are forbidden"
    echo "$legacy_temp_path_matches"
    exit 1
fi

preflight_invocation_matches=$(grep -RIn --include='*.c' --include='*.h' -E 'check_result_type_conventions\.sh' src_v2 test_v2 || true)
if [[ -n "$preflight_invocation_matches" ]]; then
    unauthorized=$(printf '%s\n' "$preflight_invocation_matches" | grep -v '^src_v2/build/test_runner_preflight.c:' || true)
    if [[ -n "$unauthorized" ]]; then
        echo "[FAIL] result type conventions preflight must be owned by src_v2/build/test_runner_preflight.c"
        echo "$unauthorized"
        exit 1
    fi
fi

for main_file in test_v2/*/test_*_main.c; do
    if ! grep -q 'test_v2_require_official_runner()' "$main_file"; then
        echo "[FAIL] test entrypoint bypasses the official test runner"
        echo "$main_file"
        exit 1
    fi
done

echo "[OK] result type conventions checks passed"
