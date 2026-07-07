TARGET = harbour-catchercatcher

CONFIG += sailfishapp

QT += core dbus network sensors

# libcrypto (OpenSSL): PBKDF2 + AES-128-CBC to decrypt the BNetzA EMF response.
# Linked directly (not via PKGCONFIG, which disturbed sailfishapp's pkg-config
# resolution and dropped -lsailfishapp from the link line).
LIBS += -lcrypto

SOURCES += \
    src/harbour-catchercatcher.cpp \
    src/cellmonitor.cpp \
    src/osmfetch.cpp \
    src/ocidlookup.cpp \
    src/bnetzalookup.cpp \
    src/sensorreader.cpp

HEADERS += \
    src/cellmonitor.h \
    src/osmfetch.h \
    src/ocidlookup.h \
    src/bnetzalookup.h \
    src/sensorreader.h

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# QML / cover
gui.files = qml
gui.path = /usr/share/$${TARGET}

# --- Tier 2 (privileged /dev/diag reader) --------------------------------
# The diag helper runs as root through pkexec, authorised by the polkit rule.
# Shipped but only started on demand from the app; Tier 1 works without it.
helper.files = data/harbour-catchercatcher-diag-helper
helper.path = /usr/bin

policy.files = data/catchercatcher.policy
policy.path = /usr/share/polkit-1/actions

desktopfile.files = $${TARGET}.desktop
desktopfile.path = /usr/share/applications

ICONPATH = /usr/share/icons/hicolor

86.png.path = $${ICONPATH}/86x86/apps/
86.png.files += icons/86x86/harbour-catchercatcher.png

108.png.path = $${ICONPATH}/108x108/apps/
108.png.files += icons/108x108/harbour-catchercatcher.png

128.png.path = $${ICONPATH}/128x128/apps/
128.png.files += icons/128x128/harbour-catchercatcher.png

172.png.path = $${ICONPATH}/172x172/apps/
172.png.files += icons/172x172/harbour-catchercatcher.png

INSTALLS += gui helper policy desktopfile \
            86.png 108.png 128.png 172.png

DISTFILES += \
    qml/harbour-catchercatcher.qml \
    qml/pages/MainPage.qml \
    qml/pages/EventLogPage.qml \
    qml/pages/MapPage.qml \
    qml/pages/AboutPage.qml \
    qml/cover/CoverPage.qml \
    data/harbour-catchercatcher-diag-helper \
    data/catchercatcher.policy \
    harbour-catchercatcher.desktop \
    rpm/harbour-catchercatcher.spec
