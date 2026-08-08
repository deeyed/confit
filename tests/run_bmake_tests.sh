#!/bin/sh

# bmake만 Confit host resolver를 조립한다. 이 runner는 현재 source graph에
# 등록된 v2 semantic tests와 sealed artifact ABI v3의 공개 행위만 검증한다.
set -eu

binary=$1
source_root=$2
object_root=$3
test_bin_root=$4
bmake=${CONFIT_BMAKE:?CONFIT_BMAKE is required}
work=$object_root/test-work
test_count=0

run()
{
    name=$1
    shift
    printf 'test: %s\n' "$name"
    "$@"
    test_count=$((test_count + 1))
}

for name in \
    status_diagnostic host_boundary parser_v2_adapter \
    schema_v2_model schema_v2_linker \
    constraint_v2 constraint_v2_validate resolver_v2_ledger \
    resolver_v2_evaluate resolver_v2_snapshot component_catalog compat_v2 \
    expression_v2 expression_v2_semantics expression_v2_fuzz parser_v2_fuzz
do
    run "unit.$name" "$test_bin_root/confit_test_$name"
done

run cli.version "$binary" --version
run cli.help "$binary" help
run cli.doctor "$binary" doctor
run cli.sealed_bundle /bin/sh "$source_root/tests/integration/bmake_v3_workflow.sh" \
    "$binary" "$source_root" "$work/bmake-v3" "$bmake"

expected_count=20
[ "$test_count" -eq "$expected_count" ] || {
    printf 'expected %s tests, ran %s\n' "$expected_count" "$test_count" >&2
    exit 1
}
printf 'confit bmake tests ok: count=%s engine=bmake\n' "$test_count"
