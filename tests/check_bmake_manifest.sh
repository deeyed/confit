#!/bin/sh

set -eu

source_root=$1
manifest=$2
work=${TMPDIR:-/tmp}/confit-manifest.$$
trap 'rm -f -- "$work.actual" "$work.listed"' EXIT HUP INT TERM

# 이 gate는 manifest 누락뿐 아니라 hard-cut된 build authority가 source tree에
# 다시 들어오는 것도 막는다. 과거 backend file은 manifest 밖에 있더라도
# source/문서 작성자가 재사용할 수 있으므로 존재 자체를 failure로 취급한다.
for forbidden in \
    "$source_root/CMakeLists.txt" \
    "$source_root/src/tui" \
    "$source_root/src/migration" \
    "$source_root/src/schema/dispatch.c" \
    "$source_root/src/resolver/dispatch.c" \
    "$source_root/src/generator/dispatch.c"
do
    if [ -e "$forbidden" ]; then
        printf 'Confit hard-cut violation: %s must not exist\n' "$forbidden" >&2
        exit 1
    fi
done

if find "$source_root" -type f \( -name '*.cmake' -o -name '*.qsm' \
    -o -name '*.qst' -o -name 'qstar.lua' \) -print -quit | grep -q .; then
    printf '%s\n' 'Confit hard-cut violation: removed backend artifact file found' >&2
    exit 1
fi

find "$source_root/src" "$source_root/tests/unit" \
    "$source_root/tests/fuzz" \
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
