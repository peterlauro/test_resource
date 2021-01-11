set(TARGET_TESTS_NAME MemoryResourceTests)

set(TARGET_TESTS_SOURCES
    main.cpp
    TestMemoryResource.cpp
    )

add_executable(${TARGET_TESTS_NAME} ${TARGET_TESTS_SOURCES})

target_link_libraries(${TARGET_TESTS_NAME}
    PRIVATE ${TARGET_NAME}
    PRIVATE gtest_main
    )

add_test(NAME ${TARGET_TESTS_NAME} COMMAND ${TARGET_TESTS_NAME}
    --gtest_output=xml:${CMAKE_BINARY_DIR}/${TARGET_TESTS_NAME}.xml
    )