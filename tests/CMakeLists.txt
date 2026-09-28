# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test)

# One executable per behaviour area; each runs offline against fixtures in
# tests/data, with no Portage tree, network or display.
foreach(test IN ITEMS tst_version tst_atom tst_depspec tst_shellvars tst_graph tst_emergeplan tst_buildlog
        tst_repoindex tst_usedescriptions tst_buildhistory)
    qt_add_executable(${test} cpp/${test}.cpp)
    target_link_libraries(${test} PRIVATE qportage_core Qt6::Test)
    target_compile_definitions(${test} PRIVATE TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

foreach(test IN ITEMS tst_agentsession tst_llmruntime)
    qt_add_executable(${test} cpp/${test}.cpp)
    target_link_libraries(${test} PRIVATE qportage_agent Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# --- ops ---
# Fake emerge/ebuild/helper scripts stand in for the real programs.
foreach(test IN ITEMS tst_plan tst_pretendrunner tst_executor)
    qt_add_executable(${test} cpp/${test}.cpp)
    target_link_libraries(${test} PRIVATE qportage_ops Qt6::Test)
    target_compile_definitions(${test} PRIVATE TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Tests of the external-source layer (qportage_sources); offline as well.
foreach(test IN ITEMS tst_sourceprobe)
    qt_add_executable(${test} cpp/${test}.cpp)
    target_link_libraries(${test} PRIVATE qportage_sources Qt6::Test)
    target_compile_definitions(${test} PRIVATE TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The root helper against a fake root (never the live system).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME helper_apply
             COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/helper/tests)
endif()
# --- end ops ---
