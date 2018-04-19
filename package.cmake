include("${CMAKE_SOURCE_DIR}/.cget/core.cmake" REQUIRED)

#CGET_HAS_DEPENDENCY(openframeworks GITHUB openframeworks/openFrameworks)
CGET_HAS_DEPENDENCY(better-enums GITHUB aantron/better-enums NO_FIND_PACKAGE NO_BUILD)
