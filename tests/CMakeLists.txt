add_executable(scanner_test scanner_test.cpp ../src/engine/scanner.cpp)
target_include_directories(scanner_test PRIVATE
    ../include
    ../third_party/vectorscan/src
    ../third_party/doctest
    ${CMAKE_BINARY_DIR}/third_party/vectorscan
)
target_link_libraries(scanner_test PRIVATE ${VECTORSCAN_TARGET} spdlog::spdlog)
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
target_compile_options(line_index_test PRIVATE
    $<$<CONFIG:Release>:-mavx2>
)
add_test(NAME line_index_test COMMAND line_index_test)

add_executable(search_controller_test
    search_controller_test.cpp
    ../src/search.cpp
    ../src/engine/scanner.cpp
    ../src/engine/source.cpp
    ../src/engine/line_index.cpp
)
target_include_directories(search_controller_test PRIVATE
    ../include
    ../third_party/vectorscan/src
    ../third_party/doctest
    ${CMAKE_BINARY_DIR}/third_party/vectorscan
)
target_link_libraries(search_controller_test PRIVATE ${VECTORSCAN_TARGET} spdlog::spdlog)
target_compile_options(search_controller_test PRIVATE
    $<$<CONFIG:Release>:-mavx2>
)
add_test(NAME search_controller_test COMMAND search_controller_test)

add_executable(parse_cli_test parse_cli_test.cpp ../src/util.cpp)
target_include_directories(parse_cli_test PRIVATE
    ../include
    ../third_party/doctest
)
add_test(NAME parse_cli_test COMMAND parse_cli_test)
