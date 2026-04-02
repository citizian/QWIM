cmake_minimum_required(VERSION 3.19)
project(QTClient LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(QTClient
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/mainwindow.ui
    src/core/app_core.cpp
    src/core/app_core.h
    src/network/network_manager.cpp
    src/network/network_manager.h
    src/models/net_message.cpp
    src/models/net_message.h
    src/models/user_model.cpp
    src/models/user_model.h
    src/config/app_config.cpp
    src/config/app_config.h
    src/utils/logger.cpp
    src/utils/logger.h
    resources/resources.qrc
)

target_include_directories(QTClient PRIVATE third_party/json)

target_link_libraries(QTClient
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
)

include(GNUInstallDirs)

install(TARGETS QTClient
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET QTClient
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
