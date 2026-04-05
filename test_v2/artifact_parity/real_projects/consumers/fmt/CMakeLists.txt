cmake_minimum_required(VERSION 3.28)
project(FmtConsumer LANGUAGES CXX)

find_package(fmt CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE fmt::fmt)
set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)
