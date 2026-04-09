cmake_minimum_required(VERSION 3.16)

project(SyncFolder VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(projectSources
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    src/PairEditDialog.cpp
    src/PairEditDialog.h
    src/PairEditDialog.ui
    src/FolderSyncWorker.cpp
    src/FolderSyncWorker.h
    src/FolderWatcher.cpp
    src/FolderWatcher.h
)

if (QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(SyncFolder
        MANUAL_FINALIZATION
        ${projectSources}
    )
else()
    add_executable(SyncFolder
        ${projectSources}
    )
endif()

target_include_directories(SyncFolder PRIVATE src)

target_link_libraries(SyncFolder PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if (MSVC)
    target_compile_options(SyncFolder PRIVATE /utf-8)
endif()

set_target_properties(SyncFolder PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if (QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_finalize_executable(SyncFolder)
endif()
