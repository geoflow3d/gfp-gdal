cmake_minimum_required(VERSION 3.21)
project (io-gdal VERSION 0.3.0)

if(PROJECT_IS_TOP_LEVEL)
  find_package(geoflow REQUIRED)
  include_directories(${geoflow_INCLUDE_DIRS})
endif()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
# GEOS include first for overriding gdal included GEOS
if (DEFINED VCPKG_TOOLCHAIN)
  find_package( GEOS CONFIG REQUIRED)
else()
  find_package( GEOS REQUIRED)
endif()
if (GEOS_FOUND)
  include_directories(${GEOS_INCLUDE_DIR})
endif()

# GDAL
find_package( GDAL 3.5 REQUIRED )
include_directories(${GDAL_INCLUDE_DIR})

set(GF_PLUGIN_NAME ${PROJECT_NAME})
set(GF_PLUGIN_TARGET_NAME "gfp_gdal")
set(GF_PLUGIN_REGISTER ${PROJECT_SOURCE_DIR}/register.hpp)
geoflow_create_plugin(gdal_nodes.cpp geos_nodes.cpp ogr_reader_node.cpp ogr_writer_node.cpp)

if (DEFINED VCPKG_TOOLCHAIN)
  target_link_libraries( gfp_gdal PRIVATE
    geoflow-core
    GEOS::geos_c
    ${GDAL_LIBRARIES}
  )
else()
  target_link_libraries( gfp_gdal PRIVATE
    geoflow-core
    geos_c
    ${GDAL_LIBRARY}
  )
endif()
if (MSVC)
  target_compile_options(gfp_gdal PRIVATE "/Zc:__cplusplus")

  # collect dll's required for runtime
  INSTALL(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Release/ DESTINATION bin
          FILES_MATCHING 
          PATTERN "*.dll"
          PATTERN "gfp*" EXCLUDE)
endif()


