cmake_minimum_required(VERSION 3.10)
project(QWIM)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the local src directory to includes for json.hpp
include_directories(${PROJECT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)

add_executable(server_bin src/server.cpp)
target_link_libraries(server_bin PRIVATE Threads::Threads)

add_executable(client_bin src/client.cpp)
target_link_libraries(client_bin PRIVATE Threads::Threads)
