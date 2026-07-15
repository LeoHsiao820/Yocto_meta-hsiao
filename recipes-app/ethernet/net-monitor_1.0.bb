SUMMARY = "Ethernet user-space network monitor"
DESCRIPTION = "User-space monitor that checks Ethernet reachability and requests net_device restart"
LICENSE = "CLOSED"

SRC_URI = "file://net_monitor.c"

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} net_monitor.c -o net_monitor
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 net_monitor ${D}${bindir}/net_monitor
}

do_create_spdx[noexec] = "1"
