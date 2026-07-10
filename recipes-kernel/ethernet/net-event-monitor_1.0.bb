SUMMARY = "Ethernet netdevice event monitor kernel module"
DESCRIPTION = "Kernel module for monitoring Linux net_device events on i.MX93 Ethernet interfaces"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://Makefile \
           file://net_event_monitor.c \
           "

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"