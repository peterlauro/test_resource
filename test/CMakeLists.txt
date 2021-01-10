set(TARGET_TEST_NAME MemoryResourceTests)

set(TARGET_TEST_SOURCES
    main.cpp
    TestMemoryResource.cpp
    )

set(TARGET_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

add_executable(${TARGET_TEST_NAME} ${TARGET_TEST_SOURCES})

target_include_directories(${TARGET_TEST_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(${TARGET_TEST_NAME} PRIVATE gtest_main)

add_test(NAME ${TARGET_TEST_NAME} COMMAND ${TARGET_TEST_NAME})