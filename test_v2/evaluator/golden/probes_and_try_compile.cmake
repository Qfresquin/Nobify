cmake_minimum_required(VERSION 3.16)
project(ProbeDemo)
try_compile(HAVE_X ${CMAKE_BINARY_DIR} probe.c)
add_executable(probe_app main.c)