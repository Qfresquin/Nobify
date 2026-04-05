cmake_minimum_required(VERSION 3.28)
project(CjsonConsumer LANGUAGES C)

find_package(cJSON CONFIG REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE cjson)
set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)
