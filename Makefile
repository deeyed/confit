_CONFIT_GNU_MAKE_GUARD := $(if $(filter 20240909,$(MAKE_VERSION)),,$(error Confit requires pinned bmake 20240909))

.if !defined(.MAKE)
.error Confit requires pinned bmake 20240909
.endif

.if ${MAKE_VERSION:Uunknown} != "20240909"
.error Confit requires pinned bmake 20240909; found ${MAKE_VERSION:Uunknown}
.endif

.include "${.PARSEDIR}/share/mk/confit.init.mk"

.MAIN: all

.PHONY: all confit tests check check-manifest clean help

all: confit

confit: ${CONFIT_BINARY}

tests: ${CONFIT_TEST_BINARIES}

check-manifest:
	@${CONFIT_SOURCE_ROOT}/tests/check_bmake_manifest.sh \
	    "${CONFIT_SOURCE_ROOT}" "${CONFIT_MANIFEST_FILE}"

check: check-manifest confit tests
	@CONFIT_BMAKE="${.MAKE}" \
	    CONFIT_ENABLE_TUI="${CONFIT_ENABLE_TUI}" \
	    ${CONFIT_SOURCE_ROOT}/tests/run_bmake_tests.sh \
	    "${CONFIT_BINARY}" "${CONFIT_SOURCE_ROOT}" \
	    "${CONFIT_OBJROOT}" "${CONFIT_TEST_BIN_ROOT}"

clean:
	@rm -f -- ${CONFIT_GENERATED_FILES}

help:
	@printf '%s\n' \
	    'Confit host build (bmake 20240909)' \
	    '' \
	    '  bmake CONFIT_OBJROOT=/absolute/output all' \
	    '  bmake CONFIT_OBJROOT=/absolute/output check' \
	    '      full parity suite; pass absolute CONFIT_LEGACY_CMAKE' \
	    '  bmake CONFIT_OBJROOT=/absolute/output CONFIT_ENABLE_TUI=no all' \
	    '' \
	    'CONFIT_HOST_CC is the host compiler; target compiler variables are ignored.'
