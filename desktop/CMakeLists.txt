
cmake_minimum_required(VERSION 3.21)

project(ColdplayDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(ColdplayDesktop
    src/main.cpp
)

target_include_directories(ColdplayDesktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(ColdplayDesktop
    PRIVATE
        Qt6::Widgets
        Qt6::Network
)
