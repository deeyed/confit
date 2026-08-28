.if !defined(_CONFIT_INIT_MK_)
_CONFIT_INIT_MK_=1

CONFIT_BUILD_API_VERSION=1
CONFIT_BMAKE_MINIMUM=20240909
CONFIT_SOURCE_ROOT:=${.PARSEDIR:H:H}
CONFIT_MANIFEST_FILE:=${CONFIT_SOURCE_ROOT}/share/mk/confit.sources.mk

.if empty(MAKE_VERSION:M[0-9]*)
.error Confit Build API ${CONFIT_BUILD_API_VERSION} requires a dated bmake release
.elif ${MAKE_VERSION} < ${CONFIT_BMAKE_MINIMUM}
.error Confit Build API ${CONFIT_BUILD_API_VERSION} requires bmake >= ${CONFIT_BMAKE_MINIMUM}; found ${MAKE_VERSION}
.endif
.if !empty(.MAKEFLAGS:M-e)
.error Confit rejects bmake -e because environment values must not override build authority
.endif

CONFIT_OBJROOT?=/tmp/confit-${.MAKE.OS:tl}-${MACHINE_ARCH:Uunknown}
.if empty(CONFIT_OBJROOT:M/*)
.error CONFIT_OBJROOT must be an absolute pre-created directory
.endif
.if ${CONFIT_OBJROOT:C,[A-Za-z0-9_./-],,g} != ""
.error CONFIT_OBJROOT contains an unsafe path character
.endif
.if !exists(${CONFIT_OBJROOT})
.error CONFIT_OBJROOT must exist before Confit is parsed
.endif
.if ${CONFIT_OBJROOT} == "/" || empty(CONFIT_OBJROOT:T) || \
    ${CONFIT_OBJROOT:T} == "."
.error CONFIT_OBJROOT must name a dedicated output leaf, not a filesystem root
.endif
CONFIT_OBJROOT_CANONICAL:=${CONFIT_OBJROOT:tA}
.if ${CONFIT_OBJROOT_CANONICAL} != ${CONFIT_OBJROOT}
.error CONFIT_OBJROOT must be canonical and must not be a symlink alias
.endif
.if !empty(CONFIT_OBJROOT:M${CONFIT_SOURCE_ROOT}) || !empty(CONFIT_OBJROOT:M${CONFIT_SOURCE_ROOT}/*)
.error CONFIT_OBJROOT must not be inside the Confit source tree
.endif

CONFIT_HOST_CC?=/usr/bin/cc
.if ${CONFIT_HOST_CC:[#]} != 1 || ${CONFIT_HOST_CC:M/*} == "" || \
    ${CONFIT_HOST_CC:C,[A-Za-z0-9_./-],,g} != "" || !exists(${CONFIT_HOST_CC})
.error CONFIT_HOST_CC must be one existing absolute compiler executable
.endif

# 이 임시 build include는 R03에서 bootstrap contract와 함께 정리한다. R02 제품
# binary는 build tool identity를 읽거나 configuration 의미로 사용하지 않는다.

.include "${CONFIT_MANIFEST_FILE}"
.include "${.PARSEDIR}/confit.host.mk"

.endif
