cmake_minimum_required(VERSION 3.25)
project(snow_linux_portal_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core DBus Gui Widgets)
enable_testing()
add_executable(snow-shot-linux-portal-tests
    ../../snow_shot/src/platform/linux/screenshotportal.cpp
    ../../snow_shot/src/platform/linux/screenshotportal.h
    ../../snow_shot/src/platform/linux/screenshotcaptureworker.cpp
    ../../snow_shot/src/platform/linux/waylandoverlay.cpp
    ../../snow_shot/src/platform/linux/windowcaptureexclusion.cpp
    ../../snow_shot/src/platform/linux/livewindownative.cpp
    ../../snow_shot/include/snow_shot/presentation/screenshotcapturecoordinator.h
    ../../snow_shot/tests/linux_screenshot_portal_tests.cpp)
target_include_directories(snow-shot-linux-portal-tests PRIVATE ../../snow_shot/src/platform/linux
    ../../snow_shot/src/presentation/capture ../../snow_shot/src ../../snow_shot/include)
target_link_libraries(snow-shot-linux-portal-tests PRIVATE Qt6::Core Qt6::DBus Qt6::Gui Qt6::Widgets)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ProjectWarnings.cmake")
snow_apply_strict_warnings(snow-shot-linux-portal-tests)
add_library(snow-shot-linux-coordinator-check OBJECT
    ../../snow_shot/src/presentation/capture/screenshotcapturecoordinator.cpp)
target_include_directories(snow-shot-linux-coordinator-check PRIVATE ../../snow_shot/include
    ../../snow-crates/crates/snow-capture-c/include)
target_link_libraries(snow-shot-linux-coordinator-check PRIVATE Qt6::Core Qt6::Gui)
snow_apply_strict_warnings(snow-shot-linux-coordinator-check)
add_executable(snow-shot-linux-shortcut-tests
    ../../snow_shot/src/platform/linux/globalshortcutbackend.cpp
    ../../snow_shot/tests/linux_global_shortcut_tests.cpp)
target_include_directories(snow-shot-linux-shortcut-tests PRIVATE
    ../../snow_shot/include ../../snow_shot/src/presentation/services)
target_link_libraries(snow-shot-linux-shortcut-tests PRIVATE Qt6::Core Qt6::DBus Qt6::Gui)
snow_apply_strict_warnings(snow-shot-linux-shortcut-tests)
add_executable(snow-shot-linux-real-shortcut-tests
    ../../snow_shot/src/platform/linux/globalshortcutbackend.cpp
    ../../snow_shot/tests/linux_real_global_shortcut_tests.cpp)
target_include_directories(snow-shot-linux-real-shortcut-tests PRIVATE
    ../../snow_shot/include ../../snow_shot/src/presentation/services)
target_link_libraries(snow-shot-linux-real-shortcut-tests PRIVATE Qt6::Core Qt6::DBus Qt6::Gui)
snow_apply_strict_warnings(snow-shot-linux-real-shortcut-tests)
add_executable(snow-shot-linux-update-tests
    ../../snow_shot/src/platform/linux/linuxupdateservice.cpp
    ../../snow_shot/include/snow_shot/update/updateservice.h
    ../../snow_shot/tests/linux_update_service_tests.cpp)
target_include_directories(snow-shot-linux-update-tests PRIVATE ../../snow_shot/include)
target_link_libraries(snow-shot-linux-update-tests PRIVATE Qt6::Core)
snow_apply_strict_warnings(snow-shot-linux-update-tests)
add_executable(snow-shot-linux-fake-portal
    ../../snow_shot/tests/linux_fake_screenshot_portal.cpp)
target_link_libraries(snow-shot-linux-fake-portal PRIVATE Qt6::Core Qt6::DBus Qt6::Gui)
snow_apply_strict_warnings(snow-shot-linux-fake-portal)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME snow-shot-linux-portal-tests
    COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:snow-shot-linux-portal-tests>)
set_tests_properties(snow-shot-linux-portal-tests PROPERTIES TIMEOUT 30 LABELS unit
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME snow-shot-linux-shortcut-tests
    COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:snow-shot-linux-shortcut-tests>)
set_tests_properties(snow-shot-linux-shortcut-tests PROPERTIES TIMEOUT 30 LABELS unit)
add_test(NAME snow-shot-linux-update-tests COMMAND snow-shot-linux-update-tests)
set_tests_properties(snow-shot-linux-update-tests PROPERTIES TIMEOUT 30 LABELS unit)
