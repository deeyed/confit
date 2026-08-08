#!/bin/sh

set -eu

binary=$1
source_root=$2
object_root=$3
test_bin_root=$4
bmake=${CONFIT_BMAKE:?CONFIT_BMAKE is required}
enable_tui=${CONFIT_ENABLE_TUI:?CONFIT_ENABLE_TUI is required}
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
    status_diagnostic model host_boundary parser_adapter parser_v2_adapter \
    schema_loader schema_dispatch schema_v2_model schema_v2_linker \
    constraint_v2 constraint_v2_validate resolver_v2_ledger \
    resolver_v2_evaluate resolver_v2_snapshot generator_v2 component_catalog compat_v2 \
    expression_v2 expression_v2_semantics graph_builder resolver explain \
    generator reports compat expression_v2_fuzz parser_v2_fuzz
do
    run "unit.$name" "$test_bin_root/confit_test_$name"
done

run cli.version "$binary" --version
run cli.help "$binary" help
run cli.doctor "$binary" doctor
run cli.explain "$binary" explain --project \
    "$source_root/tests/fixtures/schema/valid/basic" --profile sim-dsh \
    delos.debug.ddc

run cli.workflow /bin/sh "$source_root/tests/integration/round16_cli_workflow.sh" \
    "$binary" "$source_root" "$work/round16-cli"
run cli.cutover /bin/sh "$source_root/tests/integration/round16_cutover_dry_run.sh" \
    "$binary" "$source_root" "$work/round16-cutover"
run cli.realish_delos /bin/sh "$source_root/tests/integration/round11_realish_delos.sh" \
    "$binary" "$source_root" "$work/round11-realish-delos"
run cli.realish_parus /bin/sh "$source_root/tests/integration/round12_realish_parus.sh" \
    "$binary" "$source_root" "$work/round12-realish-parus"
run cli.realish_compat /bin/sh "$source_root/tests/integration/round13_realish_compat.sh" \
    "$binary" "$source_root" "$work/round13-realish-compat"

set +e
/bin/sh "$source_root/tests/integration/round8_qstar_module_artifacts.sh" \
    "$binary" "$source_root" "$work/round8-qstar"
qstar_status=$?
set -e
[ "$qstar_status" -eq 0 ] || [ "$qstar_status" -eq 77 ] || exit "$qstar_status"
test_count=$((test_count + 1))

run cli.workflow_c "$test_bin_root/confit_test_cli_workflow" \
    "$binary" "$source_root" "$work/round4-cli-c"
run cli.v2_workflow_c "$test_bin_root/confit_test_cli_v2_workflow" \
    "$binary" "$source_root" "$work/round16-cli-v2"
run integration.v2_migration_shadow \
    "$test_bin_root/confit_test_v2_migration_shadow" \
    "$binary" "$source_root" "$work/round19-migration-shadow"
run regression.v1_baseline "$test_bin_root/confit_test_v1_baseline" \
    "$binary" "$source_root" "$work/round1-v1-baseline"

if [ "$enable_tui" = yes ]; then
    run cli.tui /bin/sh "$source_root/tests/integration/round17_tui_smoke.sh" \
        "$binary" "$source_root" "$work/round17-tui"
    run cli.tui_v2 /bin/sh "$source_root/tests/integration/round17_tui_v2.sh" \
        "$binary" "$source_root" "$work/round17-tui-v2"
    run cli.tui_profile_editor /bin/sh \
        "$source_root/tests/integration/round18_tui_profile_editor.sh" \
        "$binary" "$source_root" "$work/round18-tui"
    run cli.tui_profile_create /bin/sh \
        "$source_root/tests/integration/round19_tui_profile_create.sh" \
        "$binary" "$source_root" "$work/round19-profile-create"
    run cli.tui_schema_editor /bin/sh \
        "$source_root/tests/integration/round19_tui_schema_editor.sh" \
        "$binary" "$source_root" "$work/round19-schema-editor"
fi

run cli.cli_only_lane /bin/sh \
    "$source_root/tests/integration/round2_cli_only_lane.sh" \
    "$source_root" "$work/round2-cli-only" "$bmake"
run cli.stress /bin/sh "$source_root/tests/integration/round20_stress.sh" \
    "$binary" "$source_root" "$work/round20-stress"

expected_count=43
[ "$enable_tui" = yes ] && expected_count=48
[ "$test_count" -eq "$expected_count" ] || {
    printf 'expected %s tests, ran %s\n' "$expected_count" "$test_count" >&2
    exit 1
}
printf 'confit bmake tests ok: count=%s tui=%s\n' "$test_count" "$enable_tui"
