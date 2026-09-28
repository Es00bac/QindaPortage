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
    src/core/depspec.cpp
    src/core/depspec.h
    src/core/emergeplan.cpp
    src/core/emergeplan.h
    src/core/packagegraph.cpp
    src/core/packagegraph.h
    src/core/portageconfig.cpp
    src/core/portageconfig.h
    src/core/shellvars.cpp
    src/core/shellvars.h
    src/core/vardb.cpp
    src/core/vardb.h
    src/core/version.cpp
    src/core/version.h
)
target_include_directories(qportage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(qportage_core PUBLIC QT_NO_CAST_FROM_ASCII)
target_link_libraries(qportage_core PUBLIC Qt6::Core)

# --- agent ---
# The assistant: local model lifecycle, OpenAI-compatible streaming client,
# tool registry and loop, cloud escalation. No QML; tested with a fake server.
add_library(qportage_agent STATIC
    src/agent/agentsession.cpp
    src/agent/agentsession.h
    src/agent/escalation.cpp
    src/agent/escalation.h
    src/agent/llmclient.cpp
    src/agent/llmclient.h
    src/agent/llmruntime.cpp
    src/agent/llmruntime.h
    src/agent/toolregistry.cpp
    src/agent/toolregistry.h
)
target_link_libraries(qportage_agent PUBLIC qportage_core Qt6::Core Qt6::Network)

# Developer tool: prints what the core sees on this machine.
qt_add_executable(qportage-dump tools/qportage-dump.cpp)
target_link_libraries(qportage-dump PRIVATE qportage_core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
