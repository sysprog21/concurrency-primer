#!/usr/bin/env bash
# Verify clang-format conformance for the C examples the book embeds.
#
# The version is pinned because clang-format releases disagree about this
# style, and the examples are printed verbatim in the PDF: a reformat that
# only some contributors reproduce would churn the typeset listings.
set -e -u -o pipefail

if [ -z "${CLANG_FORMAT:-}" ]; then
    if command -v clang-format-20 >/dev/null 2>&1; then
        CLANG_FORMAT="clang-format-20"
    else
        echo "Error: clang-format-20 is required (other versions differ in style)" >&2
        exit 1
    fi
fi

# One scratch file removed on any exit, rather than one per iteration that a
# mid-loop failure would leave behind.
expected=$(mktemp)
trap 'rm -f "$expected"' EXIT

ret=0
while IFS= read -r -d '' file; do
    # Keep clang-format's own diagnostics: a parse or config error otherwise
    # aborts the run with a bare exit code and nothing to act on.
    if ! err=$("$CLANG_FORMAT" --style=file "$file" 2>&1 >"$expected"); then
        echo "Error: clang-format failed on $file" >&2
        [ -n "$err" ] && printf '%s\n' "$err" >&2
        ret=1
        continue
    fi
    [ -n "$err" ] && printf '%s\n' "$err" >&2
    if ! diff -u -p --label="$file" --label="expected coding style" "$file" "$expected"; then
        ret=1
    fi
done < <(git ls-files -z -- 'examples/*.c')

exit $ret
