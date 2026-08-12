.if !defined(_CONFIT_SOURCES_MK_)
_CONFIT_SOURCES_MK_=1

# This file is the explicit bootstrap graph. Semantic grouping is review
# authority; no filesystem discovery participates in source selection.
CONFIT_CORE_SOURCES= \
	src/core/digest.c \
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
	src/schema/v5/config.c

CONFIT_EXPRESSION_AND_CONSTRAINT_SOURCES=

CONFIT_RESOLVER_SOURCES= \
	src/resolver/v5/evaluate.c \
	src/workflow/v5/workflow.c

CONFIT_GENERATOR_SOURCES= \
	src/generator/v5/generation.c

CONFIT_CLI_SOURCES= \
	src/cli/main.c \
	src/cli/v5_workflow.c

CONFIT_TEST_SUPPORT_SOURCES= \
	tests/support/test_assert.c \
	tests/support/test_fs.c \
	tests/support/test_process.c

CONFIT_UNIT_TEST_SOURCES= \
	tests/unit/test_config_v5.c \
	tests/unit/test_workflow_v5.c \
	tests/unit/test_generation_v5.c \
	tests/unit/test_status_diagnostic.c \
	tests/unit/test_host_boundary.c \
	tests/unit/test_toml_adapter.c

CONFIT_FUZZ_TEST_SOURCES= \
	tests/fuzz/test_toml_fuzz.c

.endif
