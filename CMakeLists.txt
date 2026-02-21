cmake_minimum_required(VERSION 3.16)
project(ImgCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(ImgCompare
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/CompareWidget.h
    src/CompareWidget.cpp
    src/ImageLoader.h
    src/ImageLoader.cpp
)

target_link_libraries(ImgCompare PRIVATE Qt6::Widgets)
