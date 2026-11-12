cmake_minimum_required(VERSION 3.21)
project(EnvMonitorApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Bluetooth)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(EnvMonitorApp
    main.cpp
    BleProvisioner.h
    BleProvisioner.cpp
)

qt_add_qml_module(EnvMonitorApp
    URI EnvMonitorApp
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(EnvMonitorApp PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Bluetooth
)

if(ANDROID)
    set_property(TARGET EnvMonitorApp APPEND PROPERTY
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
    set_property(TARGET EnvMonitorApp APPEND PROPERTY
        QT_ANDROID_MIN_SDK_VERSION 26)
    set_property(TARGET EnvMonitorApp APPEND PROPERTY
        QT_ANDROID_TARGET_SDK_VERSION 34)
endif()
