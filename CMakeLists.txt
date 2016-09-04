cmake_minimum_required(VERSION 3.4)
set(PROJECT_NAME_STR yorkie)
project(${PROJECT_NAME_STR})

include_directories(include)

# Set C++14
# set(CMAKE_CXX_STANDARD 14)
# set(CMAKE_CXX_STANDARD_REQUIRED on)

# Can use glob instead
# file(GLOB SOURCES "src/*.cpp")
file (GLOB YORKIE_SRC
    "include/*.h"
    "include/Infrastructure/*.h"
    "lib/Infrastructure/*.cpp"
    "lib/*.cpp"
)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Extras
# Generated from `llvm-config --cxxflags --ldflags --system-libs --libs`
# Not entirely sure why we have to strip newlines from here. :/
# TODO: Figure out how to override -isysroot properly... :(
EXEC_PROGRAM("llvm-config --cxxflags --ldflags --system-libs --libs | tr '\n' ' ' | sed 's/10.10/10.11/g'"
            OUTPUT_VARIABLE LLVM_CONFIG)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS} ${LLVM_CONFIG} -stdlib=libc++ -lc++abi")
# MESSAGE("${CMAKE_CXX_FLAGS}")

#################################################################################
# Common include directories 
#################################################################################

set(EXT_PROJECTS_DIR ${PROJECT_SOURCE_DIR}/ext)
include_directories(${LLVM_INCLUDE_DIRS})

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIRS})

# Now build our tools
add_executable(yorkie ${YORKIE_SRC})

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs support core irreader mcjit native linker)

# Link against LLVM libraries
target_link_libraries(yorkie ${llvm_libs} ${LLVM_SYSTEM_LIBS})

#################################################################################
# Tests
#################################################################################

#TODO: This is commented out for now!

# Enable testing
#enable_testing()

# Setup test target 
#set(PROJECT_TEST_NAME ${PROJECT_NAME_STR}_test)
#add_subdirectory(${EXT_PROJECTS_DIR}/gtest)

# Add test files
#file(GLOB TEST_SRC_FILES ${PROJECT_SOURCE_DIR}/test/*.cpp)
#include_directories(${GTEST_INCLUDE_DIRS} ${YORKIE_SRC})
#add_executable(${PROJECT_TEST_NAME} ${TEST_SRC_FILES})
#add_dependencies(${PROJECT_TEST_NAME} googletest)

# Link against gtest libs
#target_link_libraries(${PROJECT_TEST_NAME}
#    ${GTEST_LIBS_DIR}/libgtest.a
#    ${GTEST_LIBS_DIR}/libgtest_main.a
#)

#add_test(test1 ${PROJECT_TEST_NAME})
