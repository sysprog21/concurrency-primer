#!/usr/bin/env bash
# Every tracked C example must end with a newline.
#
# minted reproduces the file as-is, so a missing final newline shows up in the
# typeset listing as well as annoying every diff that touches the last line.
set -e -u -o pipefail

ret=0
while IFS= read -r -d '' f; do
    if [ ! -s "$f" ]; then
        # tail -c1 of an empty file is empty, which would otherwise read as a
        # correctly terminated file.
        echo "Warning: empty file $f"
        ret=1
    elif file --mime-encoding "$f" | grep -qv binary; then
        if [ -n "$(tail -c1 <"$f")" ]; then
            echo "Warning: No newline at end of file $f"
            ret=1
        fi
    fi
done < <(git ls-files -z -- 'examples/*.c')

exit $ret
