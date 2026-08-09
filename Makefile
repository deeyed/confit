.if !defined(.MAKE)
.error Confit requires bmake; GNU make is not supported
.endif

.include "${.PARSEDIR}/share/mk/confit.init.mk"

.MAIN: all

.PHONY: all confit tests check check-host check-cli check-manifest clean help

all: confit

confit: ${CONFIT_BINARY}

tests: ${CONFIT_TEST_BINARIES}

check-manifest:
	@${CONFIT_SOURCE_ROOT}/tests/check_bmake_manifest.sh \
	    "${CONFIT_SOURCE_ROOT}" "${CONFIT_MANIFEST_FILE}"

check: check-manifest confit tests
	@CONFIT_BMAKE="${.MAKE}" \
	    ${CONFIT_SOURCE_ROOT}/tests/run_bmake_tests.sh \
	    "${CONFIT_BINARY}" "${CONFIT_SOURCE_ROOT}" \
	    "${CONFIT_OBJROOT}" "${CONFIT_TEST_BIN_ROOT}"

# 이 gate는 bmake가 열거한 C test binary를 각각 직접 실행한다. Shell runner,
# 동적 argv registry와 과거 result file은 이 target의 성공 권위가 아니다.
check-host: check-cli ${CONFIT_DIRECT_TEST_TARGETS}
	@printf '%s\n' 'confit direct C host tests: pass'

check-cli: confit
	@${CONFIT_BINARY} --version
	@${CONFIT_BINARY} doctor

clean:
	@rm -f -- ${CONFIT_GENERATED_FILES}

help:
	@printf '%s\n' \
	    'Confit host build (direct bmake)' \
	    '' \
	    '  bmake CONFIT_OBJROOT=/absolute/output all' \
	    '  bmake CONFIT_OBJROOT=/absolute/output check-host' \
	    '      run every registered C host test without a shell runner' \
	    '' \
	    'CONFIT_HOST_CC is the host compiler; target compiler variables are ignored.'
