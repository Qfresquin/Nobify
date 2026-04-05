cmake_minimum_required(VERSION 3.28)
project(PugiConsumer LANGUAGES CXX)

find_package(pugixml CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE pugixml::pugixml)
set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)
