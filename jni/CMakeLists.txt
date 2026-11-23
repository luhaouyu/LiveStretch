cmake_minimum_required(VERSION 3.18.1)
project(LiveStretch)

set(CMAKE_CXX_STANDARD 17)
add_library(LiveStretch SHARED main.cpp)
target_link_libraries(LiveStretch log android)
