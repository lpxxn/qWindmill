cmake_minimum_required(VERSION 3.21)
project(qWindmill VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Test)

add_library(windmill_core
    src/device_catalog.cpp
    src/protocol.cpp
    src/query_plan.cpp
    src/collector_worker.cpp
    src/collector_worker.h
)
target_include_directories(windmill_core PUBLIC src)
target_link_libraries(windmill_core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(qWindmill
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/main_window.ui
    src/spin_arrows.qrc
)
# Windows 上的主程序使用 GUI 子系统。否则双击 EXE 会同时打开控制台，
# 关闭控制台时 Windows 也会结束附着在其上的程序。
set_target_properties(qWindmill PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_libraries(qWindmill PRIVATE windmill_core Qt6::Widgets)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(windmill_tests tests/protocol_test.cpp)
    target_link_libraries(windmill_tests PRIVATE windmill_core Qt6::Test)
    add_test(NAME windmill_tests COMMAND windmill_tests)

    qt_add_executable(windmill_window_tests
        tests/window_test.cpp
        src/main_window.cpp
        src/main_window.h
        src/main_window.ui
        src/spin_arrows.qrc
    )
    target_link_libraries(windmill_window_tests PRIVATE windmill_core Qt6::Widgets Qt6::Test)
    add_test(NAME windmill_window_tests COMMAND windmill_window_tests)
    set_tests_properties(windmill_window_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_custom_target(project_files SOURCES
    .gitignore
    README.md
    RELEASE.md
)
