DESCRIPTION = "My custom application"
LICENSE = "CLOSED"

SRC_URI = "file://22_i2c_at24c_app.c \
           file://0001-Implement-application-for-i2c-device-driver.patch \
           "

S = "${WORKDIR}/sources"
UNPACKDIR = "${S}"

do_compile() {
    ${CC} ${LDFLAGS} 22_i2c_at24c_app.c -o 22_i2c_at24c_app 
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 22_i2c_at24c_app  ${D}${bindir}
}

