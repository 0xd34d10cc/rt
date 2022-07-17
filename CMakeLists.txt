project(rt)

cmake_minimum_required(VERSION 3.22)

enable_language(ASM_MASM)

set(CMAKE_CXX_STANDARD 20)

add_executable(rt
  tests/main.cpp
  rt/cpu_context.hpp
  rt/cpu_context_win64.asm
  rt/executor.hpp
  rt/executor.cpp
  rt/io_queue.hpp
 "rt/handle.hpp" "rt/io_queue.cpp" "rt/result.hpp" "rt/socket.hpp" "rt/socket.cpp")

target_include_directories(rt PRIVATE .)

if (WIN32)
  target_compile_definitions(rt
	PRIVATE _WINSOCKAPI_
	PRIVATE WIN32_LEAN_AND_MEAN
	PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS
  )
  target_link_libraries(rt PRIVATE ws2_32 mswsock)
endif()