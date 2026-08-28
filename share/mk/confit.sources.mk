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

CONFIT_HOST_SOURCES=

CONFIT_PARSER_SOURCES= \
	vendor/tomlc17/tomlc17.c \
	src/parser/tomlc17_adapter.c

CONFIT_MODEL_AND_SCHEMA_SOURCES=

CONFIT_EXPRESSION_AND_CONSTRAINT_SOURCES=

CONFIT_RESOLVER_SOURCES=

CONFIT_GENERATOR_SOURCES=

CONFIT_CLI_SOURCES= \
	src/cli/main.c

CONFIT_TEST_SUPPORT_SOURCES= \
	tests/support/test_assert.c \
	tests/support/test_fs.c \
	tests/support/test_process.c

CONFIT_UNIT_TEST_SOURCES= \
	tests/unit/test_status_diagnostic.c \
	tests/unit/test_digest.c \
	tests/unit/test_model.c \
	tests/unit/test_toml_adapter.c \
	tests/unit/test_cli_skeleton.c \
	tests/unit/test_public_headers.c

CONFIT_INTEGRATION_TEST_SOURCES=

CONFIT_PTY_TEST_SOURCES=

CONFIT_FUZZ_TEST_SOURCES= \
	tests/fuzz/test_toml_fuzz.c

.endif
