cmake_minimum_required(VERSION 3.16)

project(SyncFolder VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network HttpServer)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network HttpServer)

# 指定输出目录 保留原来的目录结构.
if (CMAKE_BUILD_TYPE STREQUAL "Debug")

    if (MSVC)
        # 设置可执行文件的输出目录.
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/debug/msvc_x64)
    elseif (APPLE)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/debug/clang_x64)
        # 设置共享库文件的输出目录.
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/debug/clang_x64)
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/debug/gcc_x64)
        # 设置共享库文件的输出目录.
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/debug/gcc_x64)
    endif ()
else ()

    if (MSVC)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/release/msvc_x64)
    elseif (APPLE)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/release/clang_x64)
        # 设置共享库文件的输出目录.
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/release/clang_x64)
    else()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/release/gcc_x64)
        # 设置共享库文件的输出目录.
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin_Qt${QT_VERSION_MAJOR}/release/gcc_x64)
    endif ()
endif ()

message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

set(projectSources
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    src/LocalSyncTabWidget.cpp
    src/LocalSyncTabWidget.h
    src/LocalSyncTabWidget.ui
    src/HttpServerTabWidget.cpp
    src/HttpServerTabWidget.h
    src/HttpServerTabWidget.ui
    src/HttpClientTabWidget.cpp
    src/HttpClientTabWidget.h
    src/HttpClientTabWidget.ui
    src/PairEditDialog.cpp
    src/PairEditDialog.h
    src/PairEditDialog.ui
    src/FolderSyncWorker.cpp
    src/FolderSyncWorker.h
    src/FolderWatcher.cpp
    src/FolderWatcher.h
    src/HttpFolderSyncWorker.cpp
    src/HttpFolderSyncWorker.h
    src/HttpSyncServer.cpp
    src/HttpSyncServer.h
    src/SyncTypes.h
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

target_link_libraries(SyncFolder PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::HttpServer
)

if (MSVC)
    target_compile_options(SyncFolder PRIVATE /utf-8)
endif()

set_target_properties(SyncFolder PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if (QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_finalize_executable(SyncFolder)
endif()
