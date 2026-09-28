# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)

# AGENT-NOTE: this version, the Portage ebuild version, and the git tag move
# together; the ebuild pins the commit it builds (pattern: QindaLlamaWatch).
project(qindaportage
    VERSION 0.1.0
    DESCRIPTION "QindaPortage - see, plan and repair Portage with a local LLM on the QindaQt desktop"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 DBus Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

# Reading and reasoning about the system: no QML, no network, no window, so
# every parser is tested offline against fixtures.
add_library(qportage_core STATIC
    src/core/atom.cpp
    src/core/atom.h
    src/core/buildhistory.cpp
    src/core/buildhistory.h
    src/core/depspec.cpp
    src/core/depspec.h
    src/core/emergeplan.cpp
    src/core/emergeplan.h
    src/core/packagegraph.cpp
    src/core/packagegraph.h
    src/core/portageconfig.cpp
    src/core/portageconfig.h
    src/core/repoindex.cpp
    src/core/repoindex.h
    src/core/shellvars.cpp
    src/core/shellvars.h
    src/core/usedescriptions.cpp
    src/core/usedescriptions.h
    src/core/vardb.cpp
    src/core/vardb.h
    src/core/version.cpp
    src/core/version.h
)
target_include_directories(qportage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(qportage_core PUBLIC QT_NO_CAST_FROM_ASCII)
target_link_libraries(qportage_core PUBLIC Qt6::Core)

# --- sources ---
# External software: probing a repository/tarball/directory and drafting an
# ebuild for the qinda-local overlay. Network (GitHub API, raw files) and
# QProcess (git, tar); the parsers are tested offline.
add_library(qportage_sources STATIC
    src/ops/buildfiles.cpp
    src/ops/buildfiles.h
    src/ops/buildfiles_meta.cpp
    src/ops/dependencyhints.cpp
    src/ops/dependencyhints.h
    src/ops/ebuilddraft.cpp
    src/ops/ebuilddraft.h
    src/ops/sourceinfo.cpp
    src/ops/sourceinfo.h
    src/ops/sourceprobe.cpp
    src/ops/sourceprobe.h
    src/ops/sourceprobe_github.cpp
)
target_link_libraries(qportage_sources PUBLIC qportage_core Qt6::Core Qt6::Network)

# Developer tool: probe a URL and print SourceInfo, resolutions and the draft.
qt_add_executable(qportage-probe tools/qportage-probe.cpp)
target_link_libraries(qportage-probe PRIVATE qportage_sources)
# --- end sources ---

# Developer tool: prints what the core sees on this machine.
qt_add_executable(qportage-dump tools/qportage-dump.cpp)
target_link_libraries(qportage-dump PRIVATE qportage_core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
