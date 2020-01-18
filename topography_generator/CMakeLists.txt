project(topography_generator_tool)

include_directories(
  ${OMIM_ROOT}/3party/gflags/src
)

set(
  SRC
  altitudes_provider.cpp
  altitudes_provider.hpp
  generator.cpp
  main.cpp
  marching_squares/contours_builder.cpp
  marching_squares/contours_builder.hpp
  marching_squares/marching_squares.hpp
  marching_squares/square.hpp
  tile_filter.cpp
  tile_filter.hpp
  tracks_processor.cpp
  tracks_processor.hpp
  utils/values_provider.hpp
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
