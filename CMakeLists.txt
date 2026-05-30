cmake_minimum_required(VERSION 4.2.1)
project(Sleeker VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt path
set(CMAKE_PREFIX_PATH "C:/Qt/6.10.1-static")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(${PROJECT_NAME} WIN32
    main.cpp
    DesktopOverlay.cpp
    DesktopOverlay.h
    SleekPanel.cpp
    SleekPanel.h
    SleekItem.cpp
    SleekItem.h
    SleekManager.cpp
    SleekManager.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::QWindowsIntegrationPlugin
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32 user32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()