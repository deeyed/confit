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
CONFIT_OBJROOT_CANONICAL:=${CONFIT_OBJROOT:tA}
.if ${CONFIT_OBJROOT_CANONICAL} != ${CONFIT_OBJROOT}
.error CONFIT_OBJROOT must be canonical and must not be a symlink alias
.endif
.if !empty(CONFIT_OBJROOT:M${CONFIT_SOURCE_ROOT}) || !empty(CONFIT_OBJROOT:M${CONFIT_SOURCE_ROOT}/*)
.error CONFIT_OBJROOT must not be inside the Confit source tree
.endif

CONFIT_HOST_CC?=cc
.if ${CONFIT_HOST_CC:[#]} != 1 || ${CONFIT_HOST_CC:C,[A-Za-z0-9_./-],,g} != ""
.error CONFIT_HOST_CC must be one bounded compiler executable token without flags
.endif
.if !empty(CONFIT_HOST_CC:M/*) && !exists(${CONFIT_HOST_CC})
.error absolute CONFIT_HOST_CC does not exist
.endif

# Confit은 build 전에만 동작하는 구성 resolver다. TUI와 과거 build backend는
# canonical host tool의 일부가 아니므로 build option으로 되살리지 않는다.

.include "${CONFIT_MANIFEST_FILE}"
.include "${.PARSEDIR}/confit.host.mk"

.endif
