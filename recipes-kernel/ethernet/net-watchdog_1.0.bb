SUMMARY = "Ethernet net_device restart watchdog kernel module"
DESCRIPTION = "Character device kernel module for requesting net_device restart from user space"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://Makefile \
           file://net_watchdog.c \
           "

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

do_create_spdx[noexec] = "1"