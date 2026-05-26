cmake_minimum_required(VERSION 3.20)
project(threadpool VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(threadpool main.cpp threadpool.hpp threadpool.cpp)
add_executable(experiments experiments.cpp threadpool.hpp threadpool.cpp)

