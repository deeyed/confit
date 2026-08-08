#!/bin/sh

# 이 통합 검증은 host build와 함께 실행된다. 생성 결과는 caller가 준 임시
# object root 아래에만 쓰며, source tree와 실제 Parus build/를 건드리지 않는다.
set -eu

binary=$1
source_root=$2
work=$3
bmake=$4
fixture=$source_root/tests/fixtures/schema-v2/valid
first=$work/first
second=$work/second

mkdir -p "$first" "$second"

"$binary" check --project "$fixture"
"$binary" gen --project "$fixture" --artifact bundle --out "$first"
"$binary" gen --project "$fixture" --artifact bundle --out "$second"

first_generation=$(sed -n '1p' "$first/selected")
second_generation=$(sed -n '1p' "$second/selected")
test -n "$first_generation"
test "$first_generation" = "$second_generation"
first_bundle=$first/$first_generation
second_bundle=$second/$second_generation

# ABI v3은 선택·provenance·bmake adapter를 한 bundle로 묶는다. 부분 artifact나
# 과거 backend 선택은 성공할 수 없으며, 새 output에 그 이름이 남아도 안 된다.
for output in \
    config.h config.selection.json config.report.json config.inputs.json \
    config.mk config.values.mk components.mk component.catalog.json \
    config.bundle.json
do
    test -f "$first_bundle/$output"
done

diff -ru "$first_bundle" "$second_bundle"
grep -F 'artifact_abi' "$first_bundle/config.bundle.json" >/dev/null
grep -F 'PARUS_COMPONENT_IDS' "$first_bundle/components.mk" >/dev/null

if "$binary" gen --project "$fixture" --artifact cmake --out "$work/rejected"; then
    echo 'obsolete artifact request unexpectedly succeeded' >&2
    exit 1
fi
if "$binary" gen --project "$fixture" --artifact qstar --out "$work/rejected"; then
    echo 'obsolete artifact request unexpectedly succeeded' >&2
    exit 1
fi

# Pinned bmake identity is part of the canonical host contract; this check only
# proves the supplied engine is usable, not that a developer PATH is canonical.
"$bmake" -V MAKE_VERSION >/dev/null
