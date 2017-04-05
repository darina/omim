TARGET = kakasi
TEMPLATE = lib
CONFIG += staticlib

ROOT_DIR = ../..

include($$ROOT_DIR/common.pri)

CONFIG -= warn_on
CONFIG *= warn_off

DEFINES *= LIBRARY
DEFINES *= HAVE_CONFIG_H

INCLUDEPATH *= lib \
     src

HEADERS += \
    src/kakasi.h \
    lib/libkakasi.h \
    config.h

SOURCES += \
    lib/libdict.c \
    lib/libkakasi.c \
    lib/libkanjiio.c \
    lib/liba2.c \
    lib/libg2.c \
    lib/libj2.c \
    lib/libk2.c \
    lib/libee2.c \
    lib/libhh2.c \
    lib/libjj2.c \
    lib/libkk2.c \
    lib/libitaiji.c \
    lib/lib78_83.c \
    lib/liblevel.c
