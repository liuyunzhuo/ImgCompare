cmake_minimum_required(VERSION 3.16)
project(ImgCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(APP_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/CompareWidget.h
    src/CompareWidget.cpp
    src/ImageLoader.h
    src/ImageLoader.cpp
)

if (WIN32)
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    if (EXISTS "${APP_ICON_PATH}")
        # ICO header should start with 00 00 01 00
        file(READ "${APP_ICON_PATH}" APP_ICON_HEADER_HEX HEX OFFSET 0 LIMIT 4)
        if (APP_ICON_HEADER_HEX STREQUAL "00000100")
            configure_file(
                "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in"
                "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
                @ONLY
            )
            list(APPEND APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
            message(STATUS "Embedding app icon: ${APP_ICON_PATH}")
        else()
            message(WARNING "Invalid ICO data at ${APP_ICON_PATH}; icon embedding is skipped.")
        endif()
    else()
        message(STATUS "Windows icon not found: ${APP_ICON_PATH}. Add resources/app.ico to embed an app icon.")
    endif()
endif()

add_executable(ImgCompare WIN32 ${APP_SOURCES})

target_link_libraries(ImgCompare PRIVATE Qt6::Widgets)
