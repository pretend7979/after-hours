# SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
# SPDX-License-Identifier: GPL-3.0-or-later

# After Hours Arcade Manager: the game library front end. It lists every console's games and
# starts them in their emulators; Switch 2 games start in PreTenDo Emulator (eglantine.exe, built
# beside it).

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)

add_executable(afterhours WIN32
    console_list.h console_list.cpp
    console_settings.h console_settings.cpp
    console_widgets.h
    emulator_downloader.h emulator_downloader.cpp
    game_details.h game_details.cpp
    game_grid.h game_grid.cpp
    game_library.h game_library.cpp
    igdb_client.h igdb_client.cpp
    library_dialog.h library_dialog.cpp
    main.cpp
    main_window.h main_window.cpp
    metadata_dialog.h metadata_dialog.cpp
    metadata_store.h metadata_store.cpp
    paths.h
    setup_wizard.h setup_wizard.cpp
    theme.h
    types.h
    trailer_view.h trailer_view.cpp
    library.qrc
    afterhours.rc
)

target_link_libraries(afterhours PRIVATE Qt6::Widgets Qt6::Network Qt6::Concurrent)
# The version this build reports, and where its updates come from.
set(AFTERHOURS_UPDATE_REPO "pretend7979/after-hours" CACHE STRING "GitHub repository After Hours updates come from")
target_compile_definitions(afterhours PRIVATE
    AFTERHOURS_VERSION="${PROJECT_VERSION}"
    AFTERHOURS_UPDATE_REPO="${AFTERHOURS_UPDATE_REPO}")
if (WIN32)
    target_link_libraries(afterhours PRIVATE Qt6::EntryPointPrivate)
endif()

# Game details and art come from the After Hours metadata API (server/), which holds the IGDB
# account; the app carries no keys.
set(AFTERHOURS_METADATA_URL "http://54.208.61.62" CACHE STRING "After Hours metadata API address")
target_compile_definitions(afterhours PRIVATE
    AFTERHOURS_METADATA_URL="${AFTERHOURS_METADATA_URL}")

# Microsoft WebView2 SDK (BSD-style licence, externals/webview2/pkg) plays trailers in the window.
# The loader DLL ships next to the executable; the Edge WebView2 runtime comes with Windows.
if (WIN32)
    set(WEBVIEW2_SDK "${PROJECT_SOURCE_DIR}/externals/webview2/pkg/build/native")
    target_include_directories(afterhours PRIVATE "${WEBVIEW2_SDK}/include")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_custom_command(TARGET afterhours POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WEBVIEW2_SDK}/x64/WebView2Loader.dll" "$<TARGET_FILE_DIR:afterhours>")
    endif()
endif()

# The console logos the sidebar shows (dist/logos, with their licence) ship next to
# the executable.
add_custom_command(TARGET afterhours POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/dist/logos" "$<TARGET_FILE_DIR:afterhours>/logos")
