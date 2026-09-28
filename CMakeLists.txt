cmake_minimum_required(VERSION 3.21)
project(qWindmill VERSION 0.1.0 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Test)

add_library(windmill_core
    # 头文件也列入目标，Qt Creator 才会在 windmill_core 的 Header Files 中展示。
    # 它们仍由对应的 .cpp 通过 #include 使用，不会被单独编译。
    src/models.h
    src/device_catalog.cpp
    src/device_catalog.h
    src/protocol.cpp
    src/protocol.h
    src/query_plan.cpp
    src/query_plan.h
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
    src/icons.qrc
)
if(WIN32)
    # Windows 资源图标会写入 EXE，资源管理器里也能显示，不依赖 Qt 启动。
    target_sources(qWindmill PRIVATE src/app_icon.rc)
    target_include_directories(qWindmill PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
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
        src/icons.qrc
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
