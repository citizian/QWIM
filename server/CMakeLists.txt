cmake_minimum_required(VERSION 3.10)
project(QWIM_Server)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the local src directory and its subdirectories to includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/base)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/log)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/net)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/server)

# Add third-party libraries
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)

find_package(Threads REQUIRED)

add_executable(server_bin 
    src/server/main.cpp 
    src/server/IMServer.cpp 
    src/server/ChatService.cpp 
    src/server/Router.cpp 
    src/net/Connection.cpp 
    src/net/EventLoop.cpp 
    src/net/Channel.cpp 
    src/net/TimerManager.cpp 
    src/net/EventLoopThread.cpp 
    src/net/EventLoopThreadPool.cpp 
    src/log/Logger.cpp 
    src/log/LogStream.cpp 
    src/log/AsyncLogger.cpp 
    src/base/Config.cpp
)
target_link_libraries(server_bin PRIVATE Threads::Threads)

add_executable(minicl src/minicl/minicl.cpp)
target_link_libraries(minicl PRIVATE Threads::Threads)

# Stress tester is now in stress_test/ with its own CMakeLists.txt
# Run: cd stress_test && ./run.sh <server_ip> <port>
