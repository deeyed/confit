#!/bin/sh

set -eu

source_root=$1
manifest=$2
work=${TMPDIR:-/tmp}/confit-manifest.$$
trap 'rm -f -- "$work.actual" "$work.listed"' EXIT HUP INT TERM

find "$source_root/src" "$source_root/tests/unit" \
    "$source_root/tests/fuzz" "$source_root/tests/integration_c" \
    "$source_root/tests/support" -type f -name '*.c' \
    | sed "s|^$source_root/||" | sort > "$work.actual"
printf '%s\n' vendor/tomlc17/tomlc17.c >> "$work.actual"
sort -o "$work.actual" "$work.actual"

sed -n 's/^[[:space:]]*\([^#[:space:]][^[:space:]]*\.c\)[[:space:]\\]*$/\1/p' \
    "$manifest" | sort > "$work.listed"

if ! cmp -s "$work.actual" "$work.listed"; then
    printf '%s\n' 'Confit bmake source manifest is incomplete or stale:' >&2
    diff -u "$work.listed" "$work.actual" >&2 || true
    exit 1
fi

printf 'confit bmake manifest ok: %s sources\n' "$(wc -l < "$work.actual" | tr -d ' ')"
