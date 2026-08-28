.if !defined(.MAKE)
.error Confit requires bmake; GNU make is not supported
.endif

.include "${.PARSEDIR}/share/mk/confit.init.mk"

.MAIN: all

.PHONY: all confit tests check check-host check-cli help

all: confit

confit: ${CONFIT_BINARY}

tests: ${CONFIT_TEST_BINARIES}

check: check-host

# 이 gate는 bmake가 열거한 C test binary를 각각 직접 실행한다. Shell runner,
# 동적 argv registry와 과거 result file은 이 target의 성공 권위가 아니다.
check-host: check-cli ${CONFIT_DIRECT_TEST_TARGETS}

check-cli: confit
	@${CONFIT_BINARY} --version
	@${CONFIT_BINARY} help

.if ${.TARGETS:Mhelp} != ""
.info Confit host build (direct bmake)
.info /absolute/bmake CONFIT_OBJROOT=/existing/empty/output CONFIT_HOST_CC=/absolute/clang CONFIT_BMAKE_TOOL=/absolute/bmake CONFIT_SHELL=/absolute/shell all | check-host
.info Use a fresh pre-created object root instead of a destructive clean target.
.endif
help:
