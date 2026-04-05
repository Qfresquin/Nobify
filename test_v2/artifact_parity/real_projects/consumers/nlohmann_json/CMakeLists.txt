cmake_minimum_required(VERSION 3.28)
project(JsonConsumer LANGUAGES CXX)

find_package(nlohmann_json CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)
