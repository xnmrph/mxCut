cmake_minimum_required(VERSION 3.21)

project(mxcut VERSION 1.0.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 OpenGL)
include(GNUInstallDirs)

# pkg-config only supplies hints: under MSYS2 it can report POSIX paths such as
# /ucrt64/include that a native Windows CMake cannot use directly.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_MPV QUIET mpv)
endif()
find_path(MPV_INCLUDE_DIR mpv/client.h HINTS ${PC_MPV_INCLUDE_DIRS})
find_library(MPV_LIBRARY NAMES mpv libmpv HINTS ${PC_MPV_LIBRARY_DIRS})
if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
    message(FATAL_ERROR "libmpv was not found. Install its development package.")
endif()
add_library(mpv::mpv UNKNOWN IMPORTED)
set_target_properties(mpv::mpv PROPERTIES
    IMPORTED_LOCATION "${MPV_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
)

qt_standard_project_setup()

qt_add_executable(mxcut
    src/main.cpp
)

# Shared colours and icon shapes, used as QML singletons.
set_source_files_properties(qml/Theme.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(mxcut
    URI MxCut
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icons.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/Timeline.qml
        qml/FileSidebar.qml
        qml/QueuePanel.qml
        qml/SettingsDialog.qml
    SOURCES
        src/MpvItem.h src/MpvItem.cpp
        src/TrimJob.h src/TrimJob.cpp
        src/ExportQueue.h src/ExportQueue.cpp
        src/VideoFolderModel.h src/VideoFolderModel.cpp
        src/Settings.h src/Settings.cpp
        src/Encoders.h src/Encoders.cpp
        src/EncoderProbe.h src/EncoderProbe.cpp
        src/MediaPreview.h src/MediaPreview.cpp
        src/WaveformView.h src/WaveformView.cpp
)

target_link_libraries(mxcut PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::OpenGL
    mpv::mpv
)

target_include_directories(mxcut PRIVATE src)

# Window icon and header logo, built into the program.
qt_add_resources(mxcut app_icon
    PREFIX /
    FILES
        packaging/icons/mxcut-256.png
        packaging/branding/logo.png
)
target_compile_definitions(mxcut PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Icon and version details for the Windows .exe.
if(WIN32)
    set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/mxcut.ico")
    configure_file(packaging/windows/mxcut.rc.in mxcut.rc @ONLY)
    target_sources(mxcut PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mxcut.rc")
endif()

set_target_properties(mxcut PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

install(TARGETS mxcut
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(UNIX AND NOT APPLE)
    # App menu entry and icon on Linux desktops.
    install(FILES packaging/linux/mxcut.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/icons/mxcut-512.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME mxcut.png)
    install(FILES packaging/icons/mxcut-256.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME mxcut.png)
endif()

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
