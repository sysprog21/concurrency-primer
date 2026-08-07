#!/usr/bin/env bash
# Reject unsafe C idioms in the examples the book prints.
#
# Readers copy this code. The concurrency defects the manuscript keeps on
# purpose are discussed in the text; an unbounded string function smuggled in
# by a later edit would not be, so catch that class here.
set -u -o pipefail

failed=0

banned='(^|[^[:alnum:]_])(gets|sprintf|vsprintf|strcpy|stpcpy|strcat|atoi|atol|atoll|atof|mktemp|tmpnam|tempnam)[[:space:]]*\('
secrets='(password|secret|api_key|private_key|token)[[:space:]]*=[[:space:]]*"[^"]+'
# undef of either guard disables hardening whatever the value; only the define
# side needs to pin _FORTIFY_SOURCE to 0 to be worth reporting.
dangerous_pp='#[[:space:]]*(undef[[:space:]]+(_FORTIFY_SOURCE|__SSP__)|define[[:space:]]+(_FORTIFY_SOURCE[[:space:]]+0|__SSP__))'

# Blank out comments while keeping one output line per input line, so prose
# that names a banned function cannot fail the build and the line numbers
# reported below still refer to the real file.
#
# Two details are what make this safe rather than merely convenient. A comment
# becomes a space, as it does in translation phase 3, because deleting it would
# splice the tokens on either side and "_FORTIFY_SOURCE/**/0" would stop
# looking like a definition of it. And quoted literals are copied out
# untouched, honouring backslash escapes, because a string holding "/*" would
# otherwise put the scanner in a comment and swallow the code after it.
# Literals have to survive anyway: the secret pattern below matches on them.
strip_comments() {
    awk '
    BEGIN { in_block = 0; buf = ""; held = 0 }
    {
        # Translation phase 2 first: a backslash-newline is deleted before
        # anything is tokenized, whatever precedes it. Doing this ahead of the
        # scan is what keeps a spliced string, a spliced // comment, and a
        # backslash run at end of line from being read differently here than
        # by the compiler. Blank lines stand in for the lines folded away, so
        # reported line numbers keep pointing into the file.
        buf = buf $0
        if (substr($0, length($0), 1) == "\\") {
            sub(/\\$/, "", buf)
            held++
            next
        }
        emit()
    }
    END { if (buf != "" || held > 0) emit() }

    function emit(   k) {
        print scan(buf)
        for (k = 0; k < held; k++) print ""
        buf = ""; held = 0
    }

    # in_block is deliberately global: a block comment spans logical lines.
    # A literal cannot, once phase 2 has run, so quote state is local.
    function scan(s,   out, i, n, ch, nx, in_str, in_chr) {
        out = ""; i = 1; n = length(s); in_str = 0; in_chr = 0
        while (i <= n) {
            ch = substr(s, i, 1); nx = substr(s, i + 1, 1)
            if (in_block) {
                if (ch == "*" && nx == "/") { in_block = 0; i += 2 } else { i++ }
                continue
            }
            if (in_str || in_chr) {
                out = out ch
                if (ch == "\\") { out = out nx; i += 2; continue }
                if (in_str && ch == "\"") in_str = 0
                if (in_chr && ch == "'"'"'") in_chr = 0
                i++
                continue
            }
            if (ch == "\"") { in_str = 1; out = out ch; i++; continue }
            if (ch == "'"'"'") { in_chr = 1; out = out ch; i++; continue }
            if (ch == "/" && nx == "*") { in_block = 1; out = out " "; i += 2; continue }
            if (ch == "/" && nx == "/") break
            out = out ch; i++
        }
        return out
    }' "$1"
}

# Here-strings rather than "echo ... | grep -q": grep exits on first match, the
# writer takes SIGPIPE, and under pipefail the condition then reads as false,
# which would silently pass a file that does contain a banned call.
while IFS= read -r -d '' f; do
    code=$(strip_comments "$f")

    if grep -qE "$banned" <<<"$code"; then
        echo "Banned function in $f:"
        grep -nE "$banned" <<<"$code"
        failed=1
    fi
    if grep -iqE "$secrets" <<<"$code"; then
        echo "Possible hardcoded secret in $f:"
        grep -inE "$secrets" <<<"$code"
        failed=1
    fi
    if grep -qE "$dangerous_pp" <<<"$code"; then
        echo "Dangerous preprocessor directive in $f:"
        grep -nE "$dangerous_pp" <<<"$code"
        failed=1
    fi
done < <(git ls-files -z -- 'examples/*.c')

if [ $failed -eq 0 ]; then
    echo "Security checks passed."
fi

exit $failed
