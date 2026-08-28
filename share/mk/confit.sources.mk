.if !defined(_CONFIT_SOURCES_MK_)
_CONFIT_SOURCES_MK_=1

# This file is the explicit bootstrap graph. Semantic grouping is review
# authority; no filesystem discovery participates in source selection.
CONFIT_CORE_SOURCES= \
	src/core/digest.c \
	src/core/diagnostic.c \
	src/core/model.c \
	src/core/status.c \
	src/core/version.c

CONFIT_HOST_SOURCES= \
	src/host/host.c

CONFIT_PARSER_SOURCES= \
	vendor/tomlc17/tomlc17.c \
	src/parser/input_image.c \
	src/parser/tomlc17_adapter.c

CONFIT_MODEL_AND_SCHEMA_SOURCES= \
	src/schema/source_graph.c \
	src/schema/schema.c \
	src/schema/user_config.c

CONFIT_EXPRESSION_AND_CONSTRAINT_SOURCES= \
	src/expression/expression.c

CONFIT_RESOLVER_SOURCES= \
	src/resolver/resolver.c

CONFIT_SNAPSHOT_SOURCES= \
	src/snapshot/snapshot.c

CONFIT_GENERATOR_SOURCES= \
	src/emitter/emitter.c

CONFIT_CLI_SOURCES= \
	src/cli/main.c

CONFIT_TEST_SUPPORT_SOURCES= \
	tests/support/test_assert.c \
	tests/support/test_fs.c \
	tests/support/test_process.c

CONFIT_UNIT_TEST_SOURCES= \
	tests/unit/test_status_diagnostic.c \
	tests/unit/test_digest.c \
	tests/unit/test_host.c \
	tests/unit/test_input_image.c \
	tests/unit/test_model.c \
	tests/unit/test_expression.c \
	tests/unit/test_resolver.c \
	tests/unit/test_emitter.c \
	tests/unit/test_toml_adapter.c \
	tests/unit/test_cli_skeleton.c \
	tests/unit/test_public_headers.c

CONFIT_INTEGRATION_TEST_SOURCES= \
	tests/integration/test_source_graph.c \
	tests/integration/test_schema.c \
	tests/integration/test_types.c \
	tests/integration/test_config.c \
	tests/integration/test_snapshot.c \
	tests/integration/test_emitter_integration.c

CONFIT_PTY_TEST_SOURCES=

CONFIT_FUZZ_TEST_SOURCES= \
	tests/fuzz/test_toml_fuzz.c \
	tests/fuzz/test_expression_fuzz.c

.endif
