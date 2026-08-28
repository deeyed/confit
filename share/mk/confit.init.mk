.if !defined(_CONFIT_INIT_MK_)
_CONFIT_INIT_MK_=1

_CONFIT_PUBLIC_BUILD_PARAMETERS= \
	CONFIT_OBJROOT \
	CONFIT_HOST_CC \
	CONFIT_BMAKE_TOOL \
	CONFIT_SHELL

# Command-line variables have higher precedence than makefile assignments.  A
# closed override set keeps source membership, warnings, link inputs, and output
# names under the reviewed bmake graph instead of caller-controlled text.
.for _confit_override in ${.MAKEOVERRIDES}
.if empty(_CONFIT_PUBLIC_BUILD_PARAMETERS:M${_confit_override})
.error ${_confit_override} is not a public Confit build parameter
.endif
.endfor
.for _confit_parameter in ${_CONFIT_PUBLIC_BUILD_PARAMETERS}
.if empty(.MAKEOVERRIDES:M${_confit_parameter})
.error ${_confit_parameter} must be supplied explicitly on the bmake command line
.endif
.endfor

CONFIT_BUILD_API_VERSION=2
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

.if !defined(CONFIT_OBJROOT) || empty(CONFIT_OBJROOT)
.error CONFIT_OBJROOT must name one existing writable object directory
.endif
.if empty(CONFIT_OBJROOT:M/*)
.error CONFIT_OBJROOT must be an absolute pre-created directory
.endif
.if ${CONFIT_OBJROOT:C,[A-Za-z0-9_./-],,g} != ""
.error CONFIT_OBJROOT contains an unsafe path character
.endif
.if !exists(${CONFIT_OBJROOT})
.error CONFIT_OBJROOT must exist before Confit is parsed
.endif
.if !exists(${CONFIT_OBJROOT}/.)
.error CONFIT_OBJROOT must be a directory
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

.if !defined(CONFIT_HOST_CC) || empty(CONFIT_HOST_CC) || \
    ${CONFIT_HOST_CC:[#]} != 1 || ${CONFIT_HOST_CC:M/*} == "" || \
    ${CONFIT_HOST_CC:C,[A-Za-z0-9_./-],,g} != "" || !exists(${CONFIT_HOST_CC})
.error CONFIT_HOST_CC must be one existing absolute clang executable
.endif
CONFIT_HOST_CC_CANONICAL:=${CONFIT_HOST_CC:tA}

.if !defined(CONFIT_BMAKE_TOOL) || empty(CONFIT_BMAKE_TOOL) || \
    ${CONFIT_BMAKE_TOOL:[#]} != 1 || ${CONFIT_BMAKE_TOOL:M/*} == "" || \
    ${CONFIT_BMAKE_TOOL:C,[A-Za-z0-9_./-],,g} != "" || \
    !exists(${CONFIT_BMAKE_TOOL})
.error CONFIT_BMAKE_TOOL must be one existing absolute bmake executable
.endif
CONFIT_BMAKE_TOOL_CANONICAL:=${CONFIT_BMAKE_TOOL:tA}
CONFIT_RUNNING_BMAKE_CANONICAL:=${.MAKE:tA}
.if ${CONFIT_BMAKE_TOOL_CANONICAL} != ${CONFIT_RUNNING_BMAKE_CANONICAL}
.error CONFIT_BMAKE_TOOL must identify the bmake executable running this graph
.endif

.if !defined(CONFIT_SHELL) || empty(CONFIT_SHELL) || \
    ${CONFIT_SHELL:[#]} != 1 || ${CONFIT_SHELL:M/*} == "" || \
    ${CONFIT_SHELL:C,[A-Za-z0-9_./-],,g} != "" || !exists(${CONFIT_SHELL})
.error CONFIT_SHELL must be one existing absolute shell executable
.endif
CONFIT_SHELL_CANONICAL:=${CONFIT_SHELL:tA}
.SHELL: name=sh path=${CONFIT_SHELL_CANONICAL}

# These checks are shell builtins under the explicitly provisioned shell.  They
# reject non-executable tool identities and a non-writable object root before
# any compiler recipe can create or replace an output.
.BEGIN:
	@test -d ${CONFIT_OBJROOT_CANONICAL:Q}
	@test -w ${CONFIT_OBJROOT_CANONICAL:Q}
	@test -x ${CONFIT_HOST_CC_CANONICAL:Q}
	@test -x ${CONFIT_BMAKE_TOOL_CANONICAL:Q}
	@test -x ${CONFIT_SHELL_CANONICAL:Q}

# Source membership is literal and protected from command-line override.  The
# manifest may use empty named groups, but ambient variables cannot populate them.

.include "${CONFIT_MANIFEST_FILE}"
.include "${.PARSEDIR}/confit.host.mk"

.endif
