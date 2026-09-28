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
    src/core/buildlog.cpp
    src/core/buildlog.h
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

# --- ops ---
# Plans, virtual `emerge --pretend`, and the executor that drives the root
# helper. Still no QML and no network: tested offline with fake programs.
add_library(qportage_ops STATIC
    src/ops/executor.cpp
    src/ops/executor.h
    src/ops/plan.cpp
    src/ops/plan.h
    src/ops/pretendrunner.cpp
    src/ops/pretendrunner.h
)
target_link_libraries(qportage_ops PUBLIC qportage_core Qt6::Core)
# Development trees run the helper from the source tree (sudo -n).
target_compile_definitions(qportage_ops PRIVATE QINDAPORTAGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# AGENT-CONTRACT: Executor::InstalledHelper is
# ${CMAKE_INSTALL_FULL_LIBEXECDIR}/qindaportage/qindaportage-apply with the
# default /usr prefix, and the polkit policy's exec.path names the same file.
install(PROGRAMS helper/qindaportage-apply
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/qindaportage)
install(FILES helper/qportage_state.py helper/qportage_validate.py
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/qindaportage)
install(FILES helper/org.qindaqt.portage.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
# --- end ops ---

# Developer tool: prints what the core sees on this machine.
qt_add_executable(qportage-dump tools/qportage-dump.cpp)
target_link_libraries(qportage-dump PRIVATE qportage_core)

# --- app ---
# The QindaQt desktop ships the AppShell archives without CMake configs
# (owned by gui-wm/qindaqt-desktop); resolve them like QindaLlamaWatch.
find_library(QINDAQT_APP_SHELL_LIB NAMES qindaqt_app_shell PATH_SUFFIXES qt6/qml/QindaQt/AppShell)
find_library(QINDAQT_MENU_EXPORT_LIB NAMES qindaqt_app_shell_menu_export)
find_library(QINDAQT_MENU_DBUSMENU_LIB NAMES qindaqt_global_menu_dbusmenu)
find_library(QINDAQT_MENU_REGISTRAR_LIB NAMES qindaqt_global_menu_registrar)
find_library(QINDAQT_MENU_EXPORTER_LIB NAMES qindaqt_global_menu_exporter)
find_library(QINDAQT_MENU_OWNERSHIP_LIB NAMES qindaqt_global_menu_ownership)
find_library(QINDAQT_MENU_PROTOCOL_LIB NAMES qindaqt_global_menu_protocol)
find_path(QINDAQT_APP_SHELL_INCLUDE_DIR qindaqt/app_shell/application_coordinator.h)
find_path(QINDAQT_MENU_EXPORT_INCLUDE_DIR qindaqt/app_shell/menu_export/first_party_composition.h)
foreach(dependency IN ITEMS
        QINDAQT_APP_SHELL_LIB QINDAQT_MENU_EXPORT_LIB QINDAQT_MENU_DBUSMENU_LIB
        QINDAQT_MENU_REGISTRAR_LIB QINDAQT_MENU_EXPORTER_LIB QINDAQT_MENU_OWNERSHIP_LIB
        QINDAQT_MENU_PROTOCOL_LIB QINDAQT_APP_SHELL_INCLUDE_DIR QINDAQT_MENU_EXPORT_INCLUDE_DIR)
    if(NOT ${dependency})
        message(FATAL_ERROR "QindaPortage requires the QindaQt desktop libraries (${dependency}). "
                            "Install gui-wm/qindaqt-desktop first.")
    endif()
endforeach()
cmake_path(GET QINDAQT_APP_SHELL_LIB PARENT_PATH QINDAQT_APP_SHELL_LIB_DIR)

qt_add_executable(qindaportage
    src/main.cpp
    src/app/action_catalog.cpp
    src/app/action_catalog.h
    src/app/planmodel.cpp
    src/app/planmodel.h
    src/app/systemmodel.cpp
    src/app/systemmodel.h
    src/app/toolbox.cpp
    src/app/toolbox.h
    src/app/toolbox_ops.cpp
    src/app/treemap.cpp
    src/app/treemap.h
)
qt_add_qml_module(qindaportage
    URI QindaPortage
    VERSION 1.0
    # AGENT-GUARD: without this prefix the installed binary fails with
    # "module QindaPortage contains no type Main" (see QindaMPV).
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/AppMenuBar.qml
        qml/ActivityView.qml
        qml/AssistantDock.qml
        qml/ChangesView.qml
        qml/ConfigView.qml
        qml/DependencyGraphView.qml
        qml/EscalateDialog.qml
        qml/GraphView.qml
        qml/ImpactList.qml
        qml/LayerBar.qml
        qml/ModelChip.qml
        qml/OverviewView.qml
        qml/PackageInspector.qml
        qml/PackageJump.qml
        qml/PackagesView.qml
        qml/StatCard.qml
        qml/TranscriptEntry.qml
        qml/Treemap.qml
        qml/Format.js
)
target_include_directories(qindaportage PRIVATE ${QINDAQT_APP_SHELL_INCLUDE_DIR} ${QINDAQT_MENU_EXPORT_INCLUDE_DIR})
target_compile_definitions(qindaportage PRIVATE
    QINDAPORTAGE_VERSION="${PROJECT_VERSION}"
    QINDAPORTAGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(qindaportage PROPERTIES INSTALL_RPATH "${QINDAQT_APP_SHELL_LIB_DIR}")
# Static-archive order matters: dependents before dependencies.
target_link_libraries(qindaportage PRIVATE
    qportage_agent
    qportage_ops
    qportage_core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
    ${QINDAQT_MENU_EXPORT_LIB}
    ${QINDAQT_MENU_DBUSMENU_LIB}
    ${QINDAQT_MENU_REGISTRAR_LIB}
    ${QINDAQT_MENU_EXPORTER_LIB}
    ${QINDAQT_MENU_OWNERSHIP_LIB}
    ${QINDAQT_MENU_PROTOCOL_LIB}
    ${QINDAQT_APP_SHELL_LIB}
)
install(TARGETS qindaportage RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.qindaqt.Portage.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/icons/hicolor/scalable/apps/org.qindaqt.Portage.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# --- end app ---

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
