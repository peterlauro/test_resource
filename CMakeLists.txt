cmake_minimum_required (VERSION 3.8)

include(cmake/googletest.cmake)

project("StdX")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(warnings "-Wall -Wextra -pedantic -Werror")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(warnings "/W4 /WX")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(TARGET_NAME StdX)
set(TARGET_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

set(INTERFACE_SOURCES
    ${TARGET_INCLUDE_DIR}/memory_resource.h
    )

add_library(${TARGET_NAME} INTERFACE)

target_sources(${TARGET_NAME} INTERFACE ${INTERFACE_SOURCES})

target_include_directories(${TARGET_NAME} INTERFACE ${TARGET_INCLUDE_DIR})

set_target_properties(${TARGET_NAME} PROPERTIES PUBLIC_HEADER "${INTERFACE_SOURCES}")

enable_testing()
fetch_googletest(${PROJECT_SOURCE_DIR}/cmake ${PROJECT_BINARY_DIR}/googletest)
add_subdirectory(test)