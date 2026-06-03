# Tests CMakeLists.txt for mx-packageinstaller
cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Test Widgets)

set(CMAKE_AUTOMOC ON)

function(configure_mxpi_test target)
    target_link_libraries(${target}
        Qt6::Core
        Qt6::Test
        Qt6::Widgets
    )
    target_compile_options(${target} PRIVATE
        -Wpedantic
        -pedantic
        -Werror=return-type
        -Werror=switch
        -Werror=uninitialized
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(${target} PRIVATE -Werror=return-stack-address)
    else()
        target_compile_options(${target} PRIVATE -Werror=return-local-addr)
    endif()
endfunction()

add_executable(test_snapmodel
    test_snapmodel.cpp
    ../src/models/snapmodel.cpp
)
configure_mxpi_test(test_snapmodel)
add_test(NAME SnapModelTest COMMAND test_snapmodel)

add_executable(test_snapfilterproxy
    test_snapfilterproxy.cpp
    ../src/models/snapmodel.cpp
    ../src/models/snapfilterproxy.cpp
)
configure_mxpi_test(test_snapfilterproxy)
add_test(NAME SnapFilterProxyTest COMMAND test_snapfilterproxy)

add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS test_snapmodel test_snapfilterproxy
    COMMENT "Running Snap tests"
)
