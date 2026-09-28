# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test)

# One executable per behaviour area; each runs offline against fixtures in
# tests/data, with no Portage tree, network or display.
foreach(test IN ITEMS tst_version tst_atom tst_depspec tst_shellvars tst_graph tst_emergeplan tst_repoindex
        tst_usedescriptions tst_buildhistory)
    qt_add_executable(${test} cpp/${test}.cpp)
    target_link_libraries(${test} PRIVATE qportage_core Qt6::Test)
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
