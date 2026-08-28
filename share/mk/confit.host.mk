.if !defined(_CONFIT_HOST_MK_)
_CONFIT_HOST_MK_=1

# CONFIT_OBJROOT is the only output directory.  It already exists by contract,
# so ordinary targets never execute mkdir and never need nested output paths.
CONFIT_BINARY=${CONFIT_OBJROOT}/confit
CONFIT_COMPILER_CONTRACT=${CONFIT_SOURCE_ROOT}/share/mk/confit.clang.h

CONFIT_FIRST_PARTY_CFLAGS= \
	-std=c17 -Wall -Wextra -Werror -pedantic \
	-include ${CONFIT_COMPILER_CONTRACT}
CONFIT_VENDOR_CFLAGS= \
	-std=c17 -w -include ${CONFIT_COMPILER_CONTRACT}
CONFIT_INCLUDE_FLAGS= \
	-I${CONFIT_SOURCE_ROOT}/include \
	-I${CONFIT_SOURCE_ROOT}/src/parser \
	-I${CONFIT_SOURCE_ROOT}/src/schema \
	-I${CONFIT_SOURCE_ROOT}/vendor/tomlc17 \
	-I${CONFIT_SOURCE_ROOT}/tests/support
CONFIT_LINK_LIBS=
CONFIT_TEST_DEFINES= \
	-DCONFIT_TEST_SOURCE_DIR=\"${CONFIT_SOURCE_ROOT}\"

CONFIT_PRODUCT_SOURCES= \
	${CONFIT_CORE_SOURCES} \
	${CONFIT_HOST_SOURCES} \
	${CONFIT_PARSER_SOURCES} \
	${CONFIT_MODEL_AND_SCHEMA_SOURCES} \
	${CONFIT_EXPRESSION_AND_CONSTRAINT_SOURCES} \
	${CONFIT_RESOLVER_SOURCES} \
	${CONFIT_GENERATOR_SOURCES}
CONFIT_TEST_SOURCES= \
	${CONFIT_UNIT_TEST_SOURCES} \
	${CONFIT_INTEGRATION_TEST_SOURCES} \
	${CONFIT_PTY_TEST_SOURCES} \
	${CONFIT_FUZZ_TEST_SOURCES}
CONFIT_ALL_DECLARED_SOURCES= \
	${CONFIT_PRODUCT_SOURCES} \
	${CONFIT_CLI_SOURCES} \
	${CONFIT_TEST_SUPPORT_SOURCES} \
	${CONFIT_TEST_SOURCES}

.if ${CONFIT_ALL_DECLARED_SOURCES:[#]} != ${CONFIT_ALL_DECLARED_SOURCES:O:u:[#]}
.error Confit source manifest contains a duplicate translation unit
.endif
.for _src in ${CONFIT_ALL_DECLARED_SOURCES}
.if ${_src:M/*} != "" || ${_src:M*.c} == "" || ${_src:M*..*} != "" || \
    ${_src:M*//*} != "" || ${_src:C,[A-Za-z0-9_./-],,g} != ""
.error invalid literal Confit source path: ${_src}
.endif
.if !exists(${CONFIT_SOURCE_ROOT}/${_src})
.error listed Confit source does not exist: ${_src}
.endif
.endfor

CONFIT_PRODUCT_OBJECTS=
CONFIT_CLI_OBJECTS=
CONFIT_TEST_SUPPORT_OBJECTS=
CONFIT_TEST_OBJECTS=
CONFIT_TEST_BINARIES=
CONFIT_DEPFILES=

.for _src in ${CONFIT_PRODUCT_SOURCES}
_obj=${CONFIT_OBJROOT}/${_src:R:S,/,_,g}.o
CONFIT_PRODUCT_OBJECTS:=${CONFIT_PRODUCT_OBJECTS} ${_obj}
CONFIT_DEPFILES:=${CONFIT_DEPFILES} ${_obj:R}.d
${_obj}: ${CONFIT_SOURCE_ROOT}/${_src} ${CONFIT_COMPILER_CONTRACT}
.if ${_src} == "vendor/tomlc17/tomlc17.c"
	${CONFIT_HOST_CC_CANONICAL} ${CONFIT_VENDOR_CFLAGS} \
	    ${CONFIT_INCLUDE_FLAGS} -MMD -MP -MF ${.TARGET:R}.d \
	    -c ${CONFIT_SOURCE_ROOT}/${_src} -o ${.TARGET}
	@test -f ${.TARGET:Q}
.else
	${CONFIT_HOST_CC_CANONICAL} ${CONFIT_FIRST_PARTY_CFLAGS} \
	    ${CONFIT_INCLUDE_FLAGS} -MMD -MP -MF ${.TARGET:R}.d \
	    -c ${CONFIT_SOURCE_ROOT}/${_src} -o ${.TARGET}
	@test -f ${.TARGET:Q}
.endif
.endfor

.for _src in ${CONFIT_CLI_SOURCES}
_obj=${CONFIT_OBJROOT}/${_src:R:S,/,_,g}.o
CONFIT_CLI_OBJECTS:=${CONFIT_CLI_OBJECTS} ${_obj}
CONFIT_DEPFILES:=${CONFIT_DEPFILES} ${_obj:R}.d
${_obj}: ${CONFIT_SOURCE_ROOT}/${_src} ${CONFIT_COMPILER_CONTRACT}
	${CONFIT_HOST_CC_CANONICAL} ${CONFIT_FIRST_PARTY_CFLAGS} \
	    ${CONFIT_INCLUDE_FLAGS} -MMD -MP -MF ${.TARGET:R}.d \
	    -c ${CONFIT_SOURCE_ROOT}/${_src} -o ${.TARGET}
	@test -f ${.TARGET:Q}
.endfor

.for _src in ${CONFIT_TEST_SUPPORT_SOURCES}
_obj=${CONFIT_OBJROOT}/${_src:R:S,/,_,g}.o
CONFIT_TEST_SUPPORT_OBJECTS:=${CONFIT_TEST_SUPPORT_OBJECTS} ${_obj}
CONFIT_DEPFILES:=${CONFIT_DEPFILES} ${_obj:R}.d
${_obj}: ${CONFIT_SOURCE_ROOT}/${_src} ${CONFIT_COMPILER_CONTRACT}
	${CONFIT_HOST_CC_CANONICAL} ${CONFIT_FIRST_PARTY_CFLAGS} \
	    ${CONFIT_INCLUDE_FLAGS} -MMD -MP -MF ${.TARGET:R}.d \
	    -c ${CONFIT_SOURCE_ROOT}/${_src} -o ${.TARGET}
	@test -f ${.TARGET:Q}
.endfor

.for _src in ${CONFIT_TEST_SOURCES}
_obj=${CONFIT_OBJROOT}/${_src:R:S,/,_,g}.o
_bin=${CONFIT_OBJROOT}/confit_${_src:T:R}
CONFIT_TEST_OBJECTS:=${CONFIT_TEST_OBJECTS} ${_obj}
CONFIT_TEST_BINARIES:=${CONFIT_TEST_BINARIES} ${_bin}
CONFIT_DEPFILES:=${CONFIT_DEPFILES} ${_obj:R}.d
${_obj}: ${CONFIT_SOURCE_ROOT}/${_src} ${CONFIT_COMPILER_CONTRACT}
	${CONFIT_HOST_CC_CANONICAL} ${CONFIT_FIRST_PARTY_CFLAGS} \
	    ${CONFIT_INCLUDE_FLAGS} ${CONFIT_TEST_DEFINES} \
	    -MMD -MP -MF ${.TARGET:R}.d \
	    -c ${CONFIT_SOURCE_ROOT}/${_src} -o ${.TARGET}
	@test -f ${.TARGET:Q}
${_bin}: ${_obj} ${CONFIT_PRODUCT_OBJECTS} ${CONFIT_TEST_SUPPORT_OBJECTS}
	${CONFIT_HOST_CC_CANONICAL} -o ${.TARGET} ${.ALLSRC} ${CONFIT_LINK_LIBS}
	@test -x ${.TARGET:Q}
.endfor

CONFIT_ALL_OBJECTS= \
	${CONFIT_PRODUCT_OBJECTS} \
	${CONFIT_CLI_OBJECTS} \
	${CONFIT_TEST_SUPPORT_OBJECTS} \
	${CONFIT_TEST_OBJECTS}
.if ${CONFIT_ALL_OBJECTS:[#]} != ${CONFIT_ALL_OBJECTS:O:u:[#]}
.error flat object naming produced a collision
.endif

${CONFIT_BINARY}: ${CONFIT_PRODUCT_OBJECTS} ${CONFIT_CLI_OBJECTS}
	${CONFIT_HOST_CC_CANONICAL} -o ${.TARGET} ${.ALLSRC} ${CONFIT_LINK_LIBS}
	@test -x ${.TARGET:Q}

# Each test path owns one direct target.  No shell loop, directory scan, or
# runtime registry decides which tests are required.
CONFIT_DIRECT_TEST_TARGETS=
.for _test_binary in ${CONFIT_TEST_BINARIES}
CONFIT_DIRECT_TEST_TARGETS:=${CONFIT_DIRECT_TEST_TARGETS} run-${_test_binary:T}
.PHONY: run-${_test_binary:T}
run-${_test_binary:T}: ${_test_binary}
.if ${_test_binary:T} == "confit_test_cli_skeleton"
	@${_test_binary} ${CONFIT_BINARY}
.else
	@${_test_binary}
.endif
.endfor

.for _dep in ${CONFIT_DEPFILES}
.sinclude "${_dep}"
.endfor

.endif
