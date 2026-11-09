#!/bin/sh
# Runs every development check against a build of kern-checks, without a display.
# Usage: tests/run-checks.sh <build-dir>
set -u
build=${1:?usage: run-checks.sh <build-dir>}
here=$(cd "$(dirname "$0")" && pwd)
checks="$build/kern-checks"
export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=
# Checks change zoom and recent-folder preferences; never write the user's settings.
check_config=$(mktemp -d "${TMPDIR:-/tmp}/kern-check-config.XXXXXX") || exit 1
trap 'rm -rf "$check_config"' EXIT HUP INT TERM
export XDG_CONFIG_HOME="$check_config"
failed=0
run() {
    if "$checks" "$@" >/dev/null 2>&1; then
        echo "ok    $*"
    else
        echo "FAIL  $* (exit $?)"
        failed=1
    fi
}
run --self-test
run --source-editor-check
run --softbreak-check
run --mode-source-check
run --source-integrity-check
run --visual-layout-check
run --visual-edit-ranges-check
for check in --dialogs-check --dialog-keys-check --blank-area-check --unsupported-file-check \
             --file-flow-check --folder-check --new-table-keys-check --picker-keys-check --navigation-check; do
    run "$check"
done
for check in --zoom-check --interaction-check --formatted-check --insert-check --table-typing-check \
             --table-actions-check; do
    run "$check" "$here/fixtures/representative.md"
done
for check in --ui-check --editing-check --heading-check --insert-actions-check --shortcuts-check; do
    run "$check" "$here/fixtures/formatting.md"
done
for check in --recent-check --pending-edit-check; do run "$check" "$here/fixtures/formatting.md"; done
run --fresh-start-check
run --tab-close-check
run --table-layout-check "$here/fixtures/multiline-table.md"
exit $failed
