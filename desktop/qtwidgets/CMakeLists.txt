cmake_minimum_required(VERSION 3.24)
project(agentdock_qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network)

qt_add_executable(agentdock-qt WIN32
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/ngrokmanager.h
    src/ngrokmanager.cpp
    src/credentials.h
    src/credentials.cpp
    resources.qrc
)

target_link_libraries(agentdock-qt PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
if(WIN32)
    target_link_libraries(agentdock-qt PRIVATE Advapi32)
endif()

install(TARGETS agentdock-qt RUNTIME DESTINATION .)
