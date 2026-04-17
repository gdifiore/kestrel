add_executable(scanner_test scanner_test.cpp ../src/engine/scanner.cpp)
target_include_directories(scanner_test PRIVATE
    ../include
    ../third_party/vectorscan/src
    ../third_party/doctest
    ${CMAKE_BINARY_DIR}/third_party/vectorscan
)
target_link_libraries(scanner_test PRIVATE ${VECTORSCAN_TARGET})
add_test(NAME scanner_test COMMAND scanner_test)

add_executable(source_test source_test.cpp ../src/engine/source.cpp)
target_include_directories(source_test PRIVATE
    ../include
    ../third_party/doctest
)
add_test(NAME source_test COMMAND source_test)

add_executable(line_index_test line_index_test.cpp ../src/engine/line_index.cpp)
target_include_directories(line_index_test PRIVATE
    ../include
    ../third_party/doctest
)
add_test(NAME line_index_test COMMAND line_index_test)
