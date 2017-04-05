project(kakasi)

add_definitions(
    -DLIBRARY
    -DHAVE_CONFIG_H)
#-DKANWADICT=\"$(dictdir)/kanwadict\" \
#-DITAIJIDICT=\"$(dictdir)/itaijidict\"

set(CMAKE_PREFIX_PATH ./)

include_directories(lib src)

set(
  SRC
  lib/libdict.c
  lib/libkakasi.c
  lib/libkanjiio.c
  lib/liba2.c
  lib/libg2.c
  lib/libj2.c
  lib/libk2.c
  lib/libee2.c
  lib/libhh2.c
  lib/libjj2.c
  lib/libkk2.c
  lib/libitaiji.c
  lib/lib78_83.c
  lib/liblevel.c
  lib/libkakasi.h
  src/kakasi.h
  config.h
)

add_library(${PROJECT_NAME} ${SRC})
