# Tests CMakeLists.txt for mx-packageinstaller
cmake_minimum_required(VERSION 3.16)
project(mx-packageinstaller-tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 Test module
find_package(Qt6 REQUIRED COMPONENTS Test Core Widgets)

# Enable testing
enable_testing()

# Set up test include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CMAKE_AUTOMOC ON)

# PACKAGE_BACKEND is inherited from the top-level CMakeLists.txt; propagate it to every
# test target so backend-conditional code (e.g. PackageFilterProxy::isLibraryPackage())
# builds against the same variant being tested.
if(PACKAGE_BACKEND STREQUAL "pacman")
    add_compile_definitions(PACKAGE_BACKEND_PACMAN)
else()
    add_compile_definitions(PACKAGE_BACKEND_APT)
endif()

# Test for VersionNumber class
add_executable(test_versionnumber
    test_versionnumber.cpp
    ../src/versionnumber.cpp
)

target_link_libraries(test_versionnumber
    Qt6::Core
    Qt6::Test
)

target_compile_options(test_versionnumber PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

# Add compiler-specific flags for tests
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_versionnumber PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_versionnumber PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME VersionNumberTest COMMAND test_versionnumber)

# Test for PackageBackend::compareVersions() on the pacman backend (pacman-only:
# it resolves libalpm's alpm_pkg_vercmp() via dlopen/dlsym at runtime rather
# than shelling out to `vercmp` per comparison -- see packagebackend_pacman.cpp's
# doc comment for why: a subprocess per comparison is far too slow for a
# full-repo sort. There is no bundled reimplementation to unit-test in
# isolation, by design, since a hand-rolled port of libalpm's rules risks
# subtly disagreeing with the real thing). Skips gracefully (not a failure) if
# libalpm can't be resolved, since this sandbox/CI environment may not have it.
if(PACKAGE_BACKEND STREQUAL "pacman")
    add_executable(test_packagebackend_pacman
        test_packagebackend_pacman.cpp
        ../src/packagebackend_pacman.cpp
        ../src/cmd.cpp
        ../src/cmd.h
        ../src/versionnumber.cpp
    )

    target_link_libraries(test_packagebackend_pacman
        Qt6::Core
        Qt6::Test
        Qt6::Widgets
        ${CMAKE_DL_LIBS}
    )

    target_compile_definitions(test_packagebackend_pacman PRIVATE
        HELPER_PATH="${HELPER_PATH}"
    )

    target_compile_options(test_packagebackend_pacman PRIVATE
        -Wpedantic
        -pedantic
        -Werror=return-type
        -Werror=switch
        -Werror=uninitialized
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(test_packagebackend_pacman PRIVATE -Werror=return-stack-address)
    else()
        target_compile_options(test_packagebackend_pacman PRIVATE -Werror=return-local-addr)
    endif()

    add_test(NAME PackageBackendPacmanTest COMMAND test_packagebackend_pacman)
endif()

# Test for AptCache class (apt-only: hand-parses raw Debian archive Packages files,
# no pacman equivalent)
if(NOT PACKAGE_BACKEND STREQUAL "pacman")
    add_executable(test_aptcache
        test_aptcache.cpp
        ../src/aptcache.cpp
        ../src/versionnumber.cpp
    )

    target_link_libraries(test_aptcache
        Qt6::Core
        Qt6::Test
    )

    target_compile_options(test_aptcache PRIVATE
        -Wpedantic
        -pedantic
        -Werror=return-type
        -Werror=switch
        -Werror=uninitialized
    )

    # Add compiler-specific flags for tests
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(test_aptcache PRIVATE -Werror=return-stack-address)
    else()
        target_compile_options(test_aptcache PRIVATE -Werror=return-local-addr)
    endif()

    add_test(NAME AptCacheTest COMMAND test_aptcache)
endif()

# Test for PackageModel class. On the pacman backend, updateInstalledVersions()
# calls PackageBackend::compareVersions(), which resolves libalpm's
# alpm_pkg_vercmp() via dlopen/dlsym -- so packagebackend_pacman.cpp and cmd.cpp
# must be linked in too (mirroring test_cmd's HELPER_PATH below).
# testUpdateInstalledVersions() only exercises plain numeric versions, which
# libalpm and dpkg's VersionNumber agree on, so this doesn't need libalpm's
# availability to be skip-guarded the way test_packagebackend_pacman's
# dedicated tilde/suffix cases are.
set(PACKAGEMODEL_TEST_SOURCES
    test_packagemodel.cpp
    ../src/models/packagemodel.cpp
    ../src/versionnumber.cpp
)
if(PACKAGE_BACKEND STREQUAL "pacman")
    list(APPEND PACKAGEMODEL_TEST_SOURCES
        ../src/packagebackend_pacman.cpp
        ../src/cmd.cpp
        ../src/cmd.h
    )
endif()

add_executable(test_packagemodel ${PACKAGEMODEL_TEST_SOURCES})

target_link_libraries(test_packagemodel
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

if(PACKAGE_BACKEND STREQUAL "pacman")
    target_link_libraries(test_packagemodel ${CMAKE_DL_LIBS})
    target_compile_definitions(test_packagemodel PRIVATE
        HELPER_PATH="${HELPER_PATH}"
    )
endif()

target_compile_options(test_packagemodel PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_packagemodel PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_packagemodel PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME PackageModelTest COMMAND test_packagemodel)

# Test for FlatpakModel class
add_executable(test_flatpakmodel
    test_flatpakmodel.cpp
    ../src/models/flatpakmodel.cpp
)

target_link_libraries(test_flatpakmodel
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_flatpakmodel PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_flatpakmodel PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_flatpakmodel PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME FlatpakModelTest COMMAND test_flatpakmodel)

# Test for PackageFilterProxy class. Same PACKAGE_BACKEND_PACMAN reasoning as
# test_packagemodel above: PackageFilterProxy::lessThan() also calls
# PackageBackend::compareVersions() on the pacman backend, so packagebackend_pacman.cpp
# and cmd.cpp must be linked in for that build too -- though no test here actually
# sorts by the version columns, so this is purely to satisfy the linker.
set(PACKAGEFILTERPROXY_TEST_SOURCES
    test_packagefilterproxy.cpp
    ../src/models/packagemodel.cpp
    ../src/models/packagefilterproxy.cpp
    ../src/versionnumber.cpp
)
if(PACKAGE_BACKEND STREQUAL "pacman")
    list(APPEND PACKAGEFILTERPROXY_TEST_SOURCES
        ../src/packagebackend_pacman.cpp
        ../src/cmd.cpp
        ../src/cmd.h
    )
endif()

add_executable(test_packagefilterproxy ${PACKAGEFILTERPROXY_TEST_SOURCES})

target_link_libraries(test_packagefilterproxy
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

if(PACKAGE_BACKEND STREQUAL "pacman")
    target_link_libraries(test_packagefilterproxy ${CMAKE_DL_LIBS})
    target_compile_definitions(test_packagefilterproxy PRIVATE
        HELPER_PATH="${HELPER_PATH}"
    )
endif()

target_compile_options(test_packagefilterproxy PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_packagefilterproxy PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_packagefilterproxy PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME PackageFilterProxyTest COMMAND test_packagefilterproxy)

# Test for FlatpakFilterProxy class
add_executable(test_flatpakfilterproxy
    test_flatpakfilterproxy.cpp
    ../src/models/flatpakmodel.cpp
    ../src/models/flatpakfilterproxy.cpp
    ../src/versionnumber.cpp
)

target_link_libraries(test_flatpakfilterproxy
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_flatpakfilterproxy PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_flatpakfilterproxy PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_flatpakfilterproxy PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME FlatpakFilterProxyTest COMMAND test_flatpakfilterproxy)

# Test for SnapModel class
add_executable(test_snapmodel
    test_snapmodel.cpp
    ../src/models/snapmodel.cpp
)

target_link_libraries(test_snapmodel
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_snapmodel PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_snapmodel PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_snapmodel PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME SnapModelTest COMMAND test_snapmodel)

# Test for SnapFilterProxy class
add_executable(test_snapfilterproxy
    test_snapfilterproxy.cpp
    ../src/models/snapmodel.cpp
    ../src/models/snapfilterproxy.cpp
    ../src/versionnumber.cpp
)

target_link_libraries(test_snapfilterproxy
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_snapfilterproxy PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_snapfilterproxy PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_snapfilterproxy PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME SnapFilterProxyTest COMMAND test_snapfilterproxy)

# Test for PopularModel class
add_executable(test_popularmodel
    test_popularmodel.cpp
    ../src/models/popularmodel.cpp
)

target_link_libraries(test_popularmodel
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_popularmodel PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_popularmodel PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_popularmodel PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME PopularModelTest COMMAND test_popularmodel)

# Test for PopularFilterProxy class
add_executable(test_popularfilterproxy
    test_popularfilterproxy.cpp
    ../src/models/popularmodel.cpp
    ../src/models/popularfilterproxy.cpp
)

target_link_libraries(test_popularfilterproxy
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_popularfilterproxy PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_popularfilterproxy PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_popularfilterproxy PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME PopularFilterProxyTest COMMAND test_popularfilterproxy)

# Test for the Output-tab rendering helpers (carriage-return / progress handling)
add_executable(test_outputrender
    test_outputrender.cpp
)

target_link_libraries(test_outputrender
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_options(test_outputrender PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_outputrender PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_outputrender PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME OutputRenderTest COMMAND test_outputrender)

# Test that cancelling a command terminates its complete process group
add_executable(test_cmd
    test_cmd.cpp
    ../src/cmd.cpp
    ../src/cmd.h
)

target_link_libraries(test_cmd
    Qt6::Core
    Qt6::Test
    Qt6::Widgets
)

target_compile_definitions(test_cmd PRIVATE
    HELPER_PATH="${HELPER_PATH}"
)

target_compile_options(test_cmd PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_cmd PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_cmd PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME CmdTest COMMAND test_cmd)

add_executable(test_packagelistparser
    test_packagelistparser.cpp
)
target_link_libraries(test_packagelistparser Qt6::Core Qt6::Test)
add_test(NAME PackageListParserTest COMMAND test_packagelistparser)

# Test for the Flatpak size-string parsing helpers (shared, not backend-specific)
add_executable(test_sizeutils
    test_sizeutils.cpp
)

target_link_libraries(test_sizeutils
    Qt6::Core
    Qt6::Test
)

target_compile_options(test_sizeutils PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(test_sizeutils PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(test_sizeutils PRIVATE -Werror=return-local-addr)
endif()

add_test(NAME SizeUtilsTest COMMAND test_sizeutils)

# Keep the default CTest invocation usable on headless CI and local consoles.
set(ALL_TEST_NAMES
    VersionNumberTest PackageModelTest FlatpakModelTest
    PackageFilterProxyTest FlatpakFilterProxyTest SnapModelTest SnapFilterProxyTest
    PopularModelTest PopularFilterProxyTest OutputRenderTest CmdTest PackageListParserTest
    SizeUtilsTest
)
set(ALL_TEST_TARGETS
    test_versionnumber test_packagemodel test_flatpakmodel
    test_packagefilterproxy test_flatpakfilterproxy test_snapmodel test_snapfilterproxy
    test_popularmodel test_popularfilterproxy test_outputrender test_cmd
    test_packagelistparser test_sizeutils
)
if(NOT PACKAGE_BACKEND STREQUAL "pacman")
    list(APPEND ALL_TEST_NAMES AptCacheTest)
    list(APPEND ALL_TEST_TARGETS test_aptcache)
endif()
if(PACKAGE_BACKEND STREQUAL "pacman")
    list(APPEND ALL_TEST_NAMES PackageBackendPacmanTest)
    list(APPEND ALL_TEST_TARGETS test_packagebackend_pacman)
endif()

set_tests_properties(
    ${ALL_TEST_NAMES}
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Add convenience target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS ${ALL_TEST_TARGETS}
    COMMENT "Running all tests"
)

# Add file watching target for continuous testing
add_custom_target(watch_tests
    COMMAND echo "Starting continuous testing - press Ctrl+C to stop"
    COMMAND bash -c "while inotifywait -e modify -r ${CMAKE_CURRENT_SOURCE_DIR}/.. --include='.*\\.(cpp|h)$$' 2>/dev/null; do echo ''; echo '=== Files changed, rebuilding and testing ==='; ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} && ${CMAKE_CTEST_COMMAND} --verbose; done"
    COMMENT "Watch for file changes and auto-run tests"
    USES_TERMINAL
)
