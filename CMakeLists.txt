cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SCOUT_PROJECT_VERSION LIMIT_COUNT 1)
project(scout_os VERSION ${SCOUT_PROJECT_VERSION})
