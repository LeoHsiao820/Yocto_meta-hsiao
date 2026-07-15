SUMMARY = "Ethernet status inspection tool"
DESCRIPTION = "User-space utility for inspecting Ethernet interface state on i.MX93"
LICENSE = "CLOSED"

SRC_URI = "file://eth_status.c"

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} eth_status.c -o eth_status
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 eth_status ${D}${bindir}/eth_status
}
