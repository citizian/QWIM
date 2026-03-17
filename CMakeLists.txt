cmake_minimum_required(VERSION 3.10)
project(QWIM)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the local src directory to includes for json.hpp
include_directories(${PROJECT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)

add_executable(server_bin src/main.cpp src/IMServer.cpp src/Connection.cpp src/EventLoop.cpp src/Channel.cpp src/TimerManager.cpp src/EventLoopThread.cpp src/EventLoopThreadPool.cpp src/Logger.cpp src/LogStream.cpp src/AsyncLogger.cpp src/Config.cpp src/Router.cpp src/ChatService.cpp)
target_link_libraries(server_bin PRIVATE Threads::Threads)

add_executable(client_bin src/client.cpp)
target_link_libraries(client_bin PRIVATE Threads::Threads)
