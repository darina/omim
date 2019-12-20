project(terrain_generator_tool)

include_directories(
  ${OMIM_ROOT}/3party/gflags/src
)

set(
  SRC
  altitude_extractor.cpp
  altitude_extractor.hpp
  generator.cpp
  marching_squares.cpp
  marching_squares.hpp
  tracks_processor.cpp
  tracks_processor.hpp
  main.cpp
)

omim_add_executable(${PROJECT_NAME} ${SRC})

omim_link_libraries(
  ${PROJECT_NAME}
  generator
  storage
  platform
  indexer
  geometry
  coding
  base
  oauthcpp
  expat
  jansson
  protobuf
  minizip
  icu
  stats_client
  gflags
  ${LIBZ}
)

link_qt5_core(${PROJECT_NAME})
