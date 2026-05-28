cmake_minimum_required(VERSION 3.16)
project(MiMoIsland VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 path
set(CMAKE_PREFIX_PATH "C:/Qt/6.7.0/msvc2019_64")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

set(SOURCES
    src/main.cpp
    src/DynamicIsland.cpp
    src/ProfileManager.cpp
    src/FetchThread.cpp
)

set(HEADERS
    src/DynamicIsland.h
    src/ProfileManager.h
    src/FetchThread.h
)

set(RESOURCES
    resources/resources.qrc
)

set(WIN_RC
    resources/app.rc
)

add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
    ${WIN_RC}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()
