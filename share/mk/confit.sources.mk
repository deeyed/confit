.if !defined(_CONFIT_SOURCES_MK_)
_CONFIT_SOURCES_MK_=1

# This file is the explicit bootstrap graph. Semantic grouping is review
# authority; no filesystem discovery participates in source selection.
CONFIT_CORE_SOURCES= \
	src/core/diagnostic.c \
	src/core/status.c \
	src/core/version.c

CONFIT_HOST_SOURCES= \
	src/host/host_directory.c \
	src/host/host_file.c \
	src/host/host_io.c \
	src/host/host_path.c \
	src/host/host_process.c \
	src/host/host_c17_probe.c

CONFIT_PARSER_SOURCES= \
	vendor/tomlc17/tomlc17.c \
	src/parser/tomlc17_adapter.c

CONFIT_MODEL_AND_SCHEMA_SOURCES= \
	src/model/v2/model.c \
	src/schema/v4/config.c \
	src/component/catalog.c \
	src/source/catalog.c \
	src/target/build_policy.c \
	src/target/plan.c \
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
	src/resolver/v4/evaluate.c \
	src/resolver/v2/input_loader.c \
	src/resolver/v2/plan.c \
	src/resolver/v2/evaluate.c \
	src/resolver/v2/snapshot.c \
	src/resolver/v2/incremental.c

CONFIT_GENERATOR_SOURCES= \
	src/generator/v2/artifacts.c

CONFIT_CLI_SOURCES= \
	src/cli/main.c \
	src/cli/v2_workflow.c

CONFIT_TEST_SUPPORT_SOURCES= \
	tests/support/test_assert.c \
	tests/support/test_fs.c \
	tests/support/test_process.c

CONFIT_UNIT_TEST_SOURCES= \
	tests/unit/test_config_v4.c \
	tests/unit/test_status_diagnostic.c \
	tests/unit/test_host_boundary.c \
	tests/unit/test_toml_adapter.c \
	tests/unit/test_schema_v2_model.c \
	tests/unit/test_schema_v2_linker.c \
	tests/unit/test_constraint_v2.c \
	tests/unit/test_constraint_v2_validate.c \
	tests/unit/test_resolver_v2_ledger.c \
	tests/unit/test_resolver_v2_evaluate.c \
	tests/unit/test_resolver_v2_snapshot.c \
	tests/unit/test_generator_v4_publication.c \
	tests/unit/test_component_catalog.c \
	tests/unit/test_source_catalog.c \
	tests/unit/test_build_policy.c \
	tests/unit/test_expression_v2.c \
	tests/unit/test_expression_v2_semantics.c

CONFIT_FUZZ_TEST_SOURCES= \
	tests/fuzz/test_expression_v2_fuzz.c \
	tests/fuzz/test_toml_fuzz.c

.endif
