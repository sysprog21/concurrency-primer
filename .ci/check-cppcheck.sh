#!/usr/bin/env bash
# Static analysis over the C examples.
#
# --enable=warning adds to the error-severity checks rather than selecting
# them: those always run. So this does not exempt the use-after-free the
# manuscript keeps on purpose, and no suppression is needed for it either.
# cppcheck does not see it, because the free and the stale read are in
# different threads rather than along one path through a function. Style and
# portability sweeps stay off, since those would report the deliberate
# defects the book is built around.
set -e -u -o pipefail

mapfile -t SOURCES < <(git ls-files -z -- 'examples/*.c' | tr '\0' '\n')

if [ ${#SOURCES[@]} -eq 0 ]; then
    echo "No tracked C source files found."
    exit 0
fi

timeout 120 cppcheck \
    --platform=unix64 \
    --enable=warning \
    --max-configs=1 --error-exitcode=1 --inline-suppr \
    --suppress=checkersReport --suppress=unmatchedSuppression \
    --suppress=missingIncludeSystem --suppress=noValidConfiguration \
    --suppress=normalCheckLevelMaxBranches \
    --suppress=preprocessorErrorDirective \
    -D_GNU_SOURCE -D__linux__ \
    "${SOURCES[@]}"
