project(ugc)

include_directories(
  ${OMIM_ROOT}/3party/jansson/src
  ${OMIM_ROOT}/Alohalytics/src
)

set(
  SRC
  api.cpp
  api.hpp
  binary/header_v0.hpp
  binary/index_ugc.hpp
  binary/serdes.cpp
  binary/serdes.hpp
  binary/ugc_holder.hpp
  binary/visitors.hpp
  index_migration/utility.cpp
  index_migration/utility.hpp
  loader.cpp
  loader.hpp
  serdes.hpp
  serdes_json.hpp
  storage.cpp
  storage.hpp
  types.cpp
  types.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})
omim_add_test_subdirectory(ugc_tests)
