cmake_minimum_required(VERSION 3.21)
project(CloakdNative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.7 REQUIRED COMPONENTS Quick Qml)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_add_qml_module(cloakdnative SHARED
    URI Cloakd.Native VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Cloakd/Native"
    SOURCES nativegraph.h nativegraph.cpp)
target_link_libraries(cloakdnative PRIVATE Qt6::Quick)
set_target_properties(cloakdnative cloakdnativeplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Cloakd/Native"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN")
