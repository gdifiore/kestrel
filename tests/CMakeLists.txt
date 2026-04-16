add_executable(scanner_test scanner_test.cpp ../src/engine/scanner.cpp)
target_include_directories(scanner_test PRIVATE
    ../include
    ../third_party/vectorscan/src
    ../third_party/doctest
    ${CMAKE_BINARY_DIR}/third_party/vectorscan
)
target_link_libraries(scanner_test PRIVATE ${VECTORSCAN_TARGET})
add_test(NAME scanner_test COMMAND scanner_test)
