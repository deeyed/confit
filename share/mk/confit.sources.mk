.if !defined(_CONFIT_SOURCES_MK_)
_CONFIT_SOURCES_MK_=1

# This file is the explicit bootstrap graph. Semantic grouping is review
# authority; no filesystem discovery participates in source selection.
CONFIT_CORE_SOURCES= \
	src/core/diagnostic.c \
	src/core/model.c \
	src/core/status.c \
	src/core/version.c

CONFIT_HOST_SOURCES= \
	src/host/host_directory.c \
	src/host/host_file.c \
	src/host/host_io.c \
	src/host/host_path.c

CONFIT_PARSER_SOURCES= \
	src/parser/parser.c \
	src/parser/toml_scan.c \
	vendor/tomlc17/tomlc17.c \
	src/parser/v2/tomlc17_adapter.c

CONFIT_MODEL_AND_SCHEMA_SOURCES= \
	src/model/v2/model.c \
	src/schema/schema.c \
	src/schema/dispatch.c \
	src/schema/v2/loader.c \
	src/schema/v2/linker.c \
	src/schema/v2/validate.c

CONFIT_EXPRESSION_AND_CONSTRAINT_SOURCES= \
	src/expression/v2/lexer.c \
	src/expression/v2/parser.c \
	src/expression/v2/typecheck.c \
	src/expression/v2/evaluate.c \
	src/constraint/v2/choice.c \
	src/constraint/v2/explain.c \
	src/constraint/v2/graph.c \
	src/constraint/v2/validate.c

CONFIT_RESOLVER_SOURCES= \
	src/graph/graph.c \
	src/resolver/resolver.c \
	src/resolver/dispatch.c \
	src/resolver/v2/input_loader.c \
	src/resolver/v2/plan.c \
	src/resolver/v2/evaluate.c \
	src/resolver/v2/snapshot.c \
	src/resolver/v2/incremental.c \
	src/explain/explain.c

CONFIT_GENERATOR_AND_COMPAT_SOURCES= \
	src/generator/build_integration.c \
	src/generator/config_header.c \
	src/generator/reports.c \
	src/generator/value_serialization.c \
	src/generator/dispatch.c \
	src/generator/v2/artifacts.c \
	src/compat/compat.c \
	src/compat/v2/compat.c \
	src/migration/v1_to_v2.c

CONFIT_TUI_SOURCES= \
	src/tui/curses_frontend.c \
	src/tui/model_adapter_v2.c \
	src/tui/profile_editor.c \
	src/tui/schema_editor.c \
	src/tui/tui.c \
	src/tui/tui_common.c

CONFIT_TUI_UNSUPPORTED_SOURCES= \
	src/tui/tui_unsupported.c

CONFIT_CLI_SOURCES= \
	src/cli/main.c \
	src/cli/v2_workflow.c

CONFIT_TEST_SUPPORT_SOURCES= \
	tests/support/test_assert.c \
	tests/support/test_fs.c \
	tests/support/test_process.c

CONFIT_UNIT_TEST_SOURCES= \
	tests/unit/test_status_diagnostic.c \
	tests/unit/test_model.c \
	tests/unit/test_host_boundary.c \
	tests/unit/test_parser_adapter.c \
	tests/unit/test_parser_v2_adapter.c \
	tests/unit/test_schema_loader.c \
	tests/unit/test_schema_dispatch.c \
	tests/unit/test_schema_v2_model.c \
	tests/unit/test_schema_v2_linker.c \
	tests/unit/test_constraint_v2.c \
	tests/unit/test_constraint_v2_validate.c \
	tests/unit/test_resolver_v2_ledger.c \
	tests/unit/test_resolver_v2_evaluate.c \
	tests/unit/test_resolver_v2_snapshot.c \
	tests/unit/test_generator_v2.c \
	tests/unit/test_compat_v2.c \
	tests/unit/test_expression_v2.c \
	tests/unit/test_expression_v2_semantics.c \
	tests/unit/test_graph_builder.c \
	tests/unit/test_resolver.c \
	tests/unit/test_explain.c \
	tests/unit/test_generator.c \
	tests/unit/test_reports.c \
	tests/unit/test_compat.c

CONFIT_FUZZ_TEST_SOURCES= \
	tests/fuzz/test_expression_v2_fuzz.c \
	tests/fuzz/test_parser_v2_fuzz.c

CONFIT_INTEGRATION_TEST_SOURCES= \
	tests/integration_c/test_cli_workflow.c \
	tests/integration_c/test_cli_v2_workflow.c \
	tests/integration_c/test_v2_migration_shadow.c \
	tests/integration_c/test_v1_baseline.c

.endif
