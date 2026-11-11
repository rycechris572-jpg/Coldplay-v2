
cmake_minimum_required(VERSION 3.21)

project(ColdplayCompanion VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geode REQUIRED)

setup_geode_mod(${PROJECT_NAME}
    SOURCES
        src/main.cpp
)
