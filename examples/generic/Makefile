.if !defined(.MAKE)
.error this example requires bmake
.endif

CONFIT?=confit
CONFIT_CC?=clang
CONFIG?=${.CURDIR}/configs/development.toml
CONFIG_OUTPUT?=${.OBJDIR}/config
EXAMPLE_OBJROOT?=${.OBJDIR}
EXAMPLE_BINARY=${EXAMPLE_OBJROOT}/generic-example

.PHONY: configure menuconfig all

configure:
	@${CONFIT:Q} configure \
	    --root ${.CURDIR:Q} \
	    --project Confit.toml \
	    --config ${CONFIG:Q} \
	    --output ${CONFIG_OUTPUT:Q} \
	    --emit make \
	    --emit c-header \
	    --emit json

menuconfig:
	@${CONFIT:Q} menuconfig \
	    --root ${.CURDIR:Q} \
	    --project Confit.toml \
	    --config ${CONFIG:Q} \
	    --output ${CONFIG_OUTPUT:Q} \
	    --emit make \
	    --emit c-header \
	    --emit json

.if !make(configure) && !make(menuconfig)
CONFIG_VALUES_MK!=${CONFIT:Q} verify \
    --root ${.CURDIR:Q} \
    --project Confit.toml \
    --output ${CONFIG_OUTPUT:Q} \
    --print-artifact values.mk || :
CONFIG_VALUES_H!=${CONFIT:Q} verify \
    --root ${.CURDIR:Q} \
    --project Confit.toml \
    --output ${CONFIG_OUTPUT:Q} \
    --print-artifact values.h || :

.if empty(CONFIG_VALUES_MK) || empty(CONFIG_VALUES_H)
.error configuration is missing or stale; run bmake menuconfig or bmake configure
.else
.include "${CONFIG_VALUES_MK}"
.endif
.endif

SRCS=src/main.c
.if ${CONFIG_ENABLE_METRICS:Ufalse} == "true"
SRCS+=src/metrics.c
.endif

all:
	@${CONFIT_CC:Q} -std=c17 -Wall -Wextra -Werror -pedantic \
	    -include ${CONFIG_VALUES_H:Q} ${SRCS} -o ${EXAMPLE_BINARY:Q}
