cmake_minimum_required(VERSION 3.25)
project(cifa LANGUAGES C CXX)
include(CTest)

option(CIFA_CLANG_HIGH_INLINE "Use an aggressive Clang inline budget" OFF)
option(CIFA_CLANG_OPT_REMARKS "Emit Clang optimization remarks and records" OFF)
option(CIFA_COMPUTED_GOTO "Use computed-goto dispatch for hot VM opcodes when supported" OFF)
option(CIFA_ENABLE_LTO "Enable link-time optimization for Release builds" OFF)
option(CIFA_LUA_OPCODE_PROFILE "Print Lua backend dynamic opcode counts at VM destruction" OFF)
set(CIFA_CLANG_PGO_MODE "OFF" CACHE STRING "Clang PGO mode: OFF, GENERATE, or USE")
set(CIFA_CLANG_PGO_PROFILE "" CACHE FILEPATH "Merged LLVM profile data for Clang PGO USE mode")

add_library(cifa
  Cifa.cpp
  CifaBytecode.cpp)
target_include_directories(cifa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cifa PUBLIC cxx_std_23)
if(CIFA_CLANG_HIGH_INLINE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CIFA_CLANG_HIGH_INLINE requires clang-cl or Clang")
  endif()
  target_compile_options(cifa PUBLIC /clang:-mllvm /clang:-inline-threshold=10000)
endif()
if(CIFA_CLANG_OPT_REMARKS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CIFA_CLANG_OPT_REMARKS requires clang-cl or Clang")
  endif()
  target_compile_options(cifa PRIVATE
    /clang:-fsave-optimization-record
    /clang:-Rpass=inline
    /clang:-Rpass-missed=inline
    /clang:-Rpass-analysis=inline
    /clang:-Rpass=loop-vectorize
    /clang:-Rpass-missed=loop-vectorize
    /clang:-Rpass-analysis=loop-vectorize
    /clang:-Rpass=loop-unroll
    /clang:-Rpass-missed=loop-unroll
    /clang:-Rpass-analysis=loop-unroll)
endif()
if(NOT CIFA_CLANG_PGO_MODE STREQUAL "OFF")
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CIFA_CLANG_PGO_MODE requires clang-cl or Clang")
  endif()
  if(CIFA_CLANG_PGO_MODE STREQUAL "GENERATE")
    target_compile_options(cifa PUBLIC /clang:-fprofile-instr-generate)
  elseif(CIFA_CLANG_PGO_MODE STREQUAL "USE")
    if(CIFA_CLANG_PGO_PROFILE STREQUAL "")
      message(FATAL_ERROR "CIFA_CLANG_PGO_PROFILE is required for CIFA_CLANG_PGO_MODE=USE")
    endif()
    target_compile_options(cifa PUBLIC /clang:-fprofile-instr-use=${CIFA_CLANG_PGO_PROFILE})
  else()
    message(FATAL_ERROR "CIFA_CLANG_PGO_MODE must be OFF, GENERATE, or USE")
  endif()
endif()
if(CIFA_COMPUTED_GOTO)
  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CIFA_COMPUTED_GOTO requires Clang or GCC")
  endif()
  target_compile_definitions(cifa PRIVATE CIFA_COMPUTED_GOTO=1)
endif()
if(CIFA_LUA_OPCODE_PROFILE)
  target_compile_definitions(cifa PRIVATE CIFA_LUA_OPCODE_PROFILE=1)
endif()
if(CIFA_ENABLE_LTO)
  set_property(TARGET cifa PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
if(MSVC)
  target_compile_options(cifa PUBLIC /utf-8 /Zi)
  target_link_options(cifa INTERFACE /DEBUG /INCREMENTAL:NO)
else()
  target_compile_options(cifa PRIVATE -g -fno-omit-frame-pointer)
endif()
add_executable(cifa_lua_backend_test unit_test/cifa_lua_backend_test.cpp)
target_link_libraries(cifa_lua_backend_test PRIVATE cifa)
add_test(NAME lua_backend COMMAND cifa_lua_backend_test)
set_tests_properties(lua_backend PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(cifa_lua_backend_benchmark benchmarks/lua_backend_benchmark.cpp)
target_link_libraries(cifa_lua_backend_benchmark PRIVATE cifa)
