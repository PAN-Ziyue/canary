cmake_minimum_required(VERSION 3.10.0)
project(canary C CXX)

unset(LLVMCONFIG CACHE)
find_program(LLVMCONFIG llvm-config)
if (NOT LLVMCONFIG)
    message(STATUS "llvm-config not found!")
    find_program(LLVMCONFIG llvm-config-12)
    if (NOT LLVMCONFIG)
        message(FATAL_ERROR "llvm-config-12 not found!")
    else ()
        message(STATUS "llvm-config-12 found!")
    endif ()
endif ()

execute_process(COMMAND ${LLVMCONFIG} --cxxflags OUTPUT_VARIABLE CMAKE_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVMCONFIG} --cflags OUTPUT_VARIABLE CMAKE_C_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVMCONFIG} --cppflags OUTPUT_VARIABLE CMAKE_CPP_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVMCONFIG} --ldflags OUTPUT_VARIABLE CMAKE_EXE_LINKER_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVMCONFIG} --system-libs OUTPUT_VARIABLE LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVMCONFIG} --includedir OUTPUT_VARIABLE LLVM_INCLUDE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LIBS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fexceptions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BUILD_TESTS "build all unit tests" ON)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(AFTER ${CMAKE_CURRENT_SOURCE_DIR}/include)


# find Z3
find_library(
  Z3_LIBRARIES
  NAMES z3
  HINTS ${Z3_DIR} ENV Z3_DIR
  PATH_SUFFIXES bin lib)
find_path(
  Z3_INCLUDES
  NAMES z3++.h
  HINTS ${Z3_DIR} ENV Z3_DIR
  PATH_SUFFIXES include z3)
if(NOT Z3_LIBRARIES OR NOT Z3_INCLUDES)
  message(FATAL_ERROR "Z3 not found!")
endif()
message(STATUS "Found Z3: ${Z3_LIBRARIES}")
message(STATUS "Z3 include dir: ${Z3_INCLUDES}")

include_directories(${Z3_INCLUDES})
                    
add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(benchmarks)

if (BUILD_TESTS)
	add_subdirectory (test)
endif()

enable_testing ()
add_test (AliasTest ${PROJECT_BINARY_DIR}/test/AliasTest)
