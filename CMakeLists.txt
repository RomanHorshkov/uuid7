cmake_minimum_required(VERSION 3.16)

project(UUID7 C)

option(UUID7_BUILD_TESTS "Build UUID7 unit tests" ON)
option(UUID7_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

set(UUID7_HEADERS include/uuid7.h)
set(UUID7_SOURCES src/uuid7.c)
set(UUID7_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(uuid7_obj OBJECT ${UUID7_SOURCES})
target_include_directories(
    uuid7_obj
    PUBLIC
        $<BUILD_INTERFACE:${UUID7_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(uuid7_obj PUBLIC c_std_11)
set_target_properties(uuid7_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

define_property(TARGET PROPERTY UUID7_EXPORT_NAME BRIEF_DOCS "" FULL_DOCS "")

add_library(uuid7_static STATIC $<TARGET_OBJECTS:uuid7_obj>)
add_library(uuid7_shared SHARED $<TARGET_OBJECTS:uuid7_obj>)

set_target_properties(uuid7_static PROPERTIES OUTPUT_NAME uuid7)
set_target_properties(uuid7_shared PROPERTIES OUTPUT_NAME uuid7)

foreach(lib uuid7_static uuid7_shared)
    target_include_directories(
        ${lib}
        PUBLIC
            $<BUILD_INTERFACE:${UUID7_PUBLIC_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${lib} PUBLIC c_std_11)
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(UUID7_WARNING_FLAGS -Wall -Wextra -Wpedantic)
    target_compile_options(uuid7_obj PRIVATE ${UUID7_WARNING_FLAGS})
endif()

if(UUID7_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(uuid7_obj PRIVATE --coverage -O0 -g)
        target_link_options(uuid7_shared PRIVATE --coverage)
        set(UUID7_TEST_COVERAGE_OPTIONS --coverage)
    else()
        message(WARNING "Coverage is only supported with GCC or Clang; ignoring UUID7_ENABLE_COVERAGE")
    endif()
endif()

add_library(uuid7::static ALIAS uuid7_static)
add_library(uuid7::shared ALIAS uuid7_shared)

include(GNUInstallDirs)

install(TARGETS uuid7_static uuid7_shared
        EXPORT UUID7Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT UUID7Targets
        NAMESPACE uuid7::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uuid7)

if(UUID7_BUILD_TESTS)
    enable_testing()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

    add_executable(uuid7_tests tests/test_uuid7.c)
    target_link_libraries(uuid7_tests PRIVATE uuid7_static PkgConfig::CMOCKA)
    if(UUID7_ENABLE_COVERAGE AND UUID7_TEST_COVERAGE_OPTIONS)
        target_link_options(uuid7_tests PRIVATE ${UUID7_TEST_COVERAGE_OPTIONS})
    endif()
    add_test(NAME uuid7.unit COMMAND uuid7_tests)
endif()
