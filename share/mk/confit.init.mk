.if !defined(_CONFIT_INIT_MK_)
_CONFIT_INIT_MK_=1

CONFIT_BUILD_API_VERSION=1
CONFIT_BMAKE_VERSION=20240909
CONFIT_SOURCE_ROOT:=${.PARSEDIR:H:H}
CONFIT_MANIFEST_FILE:=${CONFIT_SOURCE_ROOT}/share/mk/confit.sources.mk

.if ${MAKE_VERSION:Uunknown} != "${CONFIT_BMAKE_VERSION}"
.error Confit Build API ${CONFIT_BUILD_API_VERSION} requires bmake ${CONFIT_BMAKE_VERSION}
.endif

CONFIT_OBJROOT?=/tmp/confit-${.MAKE.OS:tl}-${MACHINE_ARCH:Uunknown}
.if empty(CONFIT_OBJROOT:M/*)
.error CONFIT_OBJROOT must be an absolute path outside the source tree
.endif
.if !empty(CONFIT_OBJROOT:M${CONFIT_SOURCE_ROOT}*)
.error CONFIT_OBJROOT must not be inside the Confit source tree
.endif

CONFIT_HOST_CC?=/usr/bin/cc
.if empty(CONFIT_HOST_CC:M/*)
.error CONFIT_HOST_CC must be an absolute host compiler path
.endif
CONFIT_HOST_CC_FAMILY?=clang
.if ${CONFIT_HOST_CC_FAMILY} != "clang" && ${CONFIT_HOST_CC_FAMILY} != "gcc"
.error CONFIT_HOST_CC_FAMILY must be clang or gcc
.endif

# Confit은 build 전에만 동작하는 구성 resolver다. TUI와 과거 build backend는
# canonical host tool의 일부가 아니므로 build option으로 되살리지 않는다.

.include "${CONFIT_MANIFEST_FILE}"
.include "${.PARSEDIR}/confit.host.mk"

.endif
