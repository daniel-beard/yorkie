cmake_minimum_required(VERSION 3.4)
project(yorkie)

include_directories(include)

# Set C++11
set (CMAKE_CXX_STANDARD 11)

# Can use glob instead
# file(GLOB SOURCES "src/*.cpp")
file (GLOB YORKIE_SRC
    "include/*.h"
    "lib/Parser.cpp"
    "lib/Lexer.cpp"
    "lib/toy.cpp"
)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Extras
# Generated from `llvm-config --cxxflags --ldflags --system-libs --libs`
# Not entirely sure why we have to strip newlines from here. :/
EXEC_PROGRAM("llvm-config --cxxflags --ldflags --system-libs --libs | tr '\n' ' '"
            OUTPUT_VARIABLE LLVM_CONFIG)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS} ${LLVM_CONFIG}")

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIRS})

# Now build our tools
add_executable(yorkie ${YORKIE_SRC})

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs support core irreader mcjit native linker)

# Link against LLVM libraries
target_link_libraries(yorkie ${llvm_libs} ${LLVM_SYSTEM_LIBS})
