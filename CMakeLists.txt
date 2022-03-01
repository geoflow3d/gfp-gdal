cmake_minimum_required(VERSION 3.14)
project (io-gdal VERSION 0.0.1)

if(PROJECT_IS_TOP_LEVEL)
  find_package(geoflow REQUIRED)
  include_directories(${geoflow_INCLUDE_DIRS})
endif()

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
# GEOS include first for overriding gdal included GEOS
find_package( GEOS CONFIG REQUIRED)
if (GEOS_FOUND)
  include_directories(${GEOS_INCLUDE_DIR})
endif()

# GDAL
find_package( GDAL )
include_directories(${GDAL_INCLUDE_DIR})

set(GF_PLUGIN_NAME ${PROJECT_NAME})
set(GF_PLUGIN_TARGET_NAME "gfp_gdal")
set(GF_PLUGIN_REGISTER ${PROJECT_SOURCE_DIR}/register.hpp)
geoflow_create_plugin(gdal_nodes.cpp geos_nodes.cpp postgis_node.cpp)

target_link_libraries( gfp_gdal PRIVATE
  geoflow-core
  GEOS::geos_c
  ${GDAL_LIBRARY}
)
if (MSVC)
  target_compile_options(gfp_gdal PRIVATE "/Zc:__cplusplus")
endif()
