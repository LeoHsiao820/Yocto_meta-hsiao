DESCRIPTION = "My custom application"
LICENSE = "CLOSED"

SRC_URI = "file://16_gpioled_platform_app.c"

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

do_compile() {
    ${CC} ${LDFLAGS} 16_gpioled_platform_app.c -o 16_gpioled_platform_app 
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 16_gpioled_platform_app  ${D}${bindir}
}

