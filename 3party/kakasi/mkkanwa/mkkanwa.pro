TARGET = mkkanwa
TEMPLATE = app
CONFIG -= app_bundle

ROOT_DIR = ../../..

include($$ROOT_DIR/common.pri)

DEFINES *= HAVE_CONFIG_H
DEFINES *= ITAIJIDICT=\\\"$$ROOT_DIR/3party/kakasi/itaijidict\\\"

INCLUDEPATH *= ../ \
    ../src

HEADERS += \
    ../src/kakasi.h \
    ../config.h

SOURCES += \
    ../src/mkkanwa.c \
    ../src/dict.c \
    ../src/itaiji.c \
    ../src/getopt.c \
    ../src/getopt1.c
