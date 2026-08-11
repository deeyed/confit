.if !defined(.MAKE)
.error Confit requires bmake; GNU make is not supported
.endif

.include "${.PARSEDIR}/share/mk/confit.init.mk"

.MAIN: all

.PHONY: all confit tests check check-host check-cli clean help

all: confit

confit: ${CONFIT_BINARY}

tests: ${CONFIT_TEST_BINARIES}

check: check-host

# 이 gate는 bmake가 열거한 C test binary를 각각 직접 실행한다. Shell runner,
# 동적 argv registry와 과거 result file은 이 target의 성공 권위가 아니다.
check-host: check-cli ${CONFIT_DIRECT_TEST_TARGETS}

check-cli: confit
	@${CONFIT_BINARY} --version
	@${CONFIT_BINARY} doctor

clean:
	@/bin/rm -f -- ${CONFIT_GENERATED_FILES}

.if ${.TARGETS:Mhelp} != ""
.info Confit host build (direct bmake)
.info bmake CONFIT_OBJROOT=/absolute/output all | check-host
.info CONFIT_HOST_CC is the absolute host compiler.
.endif
help:
