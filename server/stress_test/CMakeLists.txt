cmake_minimum_required(VERSION 3.10)
project(QWIM_StressTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(stress_tester stress_tester.cpp)
target_link_libraries(stress_tester PRIVATE Threads::Threads)
