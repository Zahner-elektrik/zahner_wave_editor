find_package(Qt6 REQUIRED COMPONENTS Test)

# One test executable per core area. New test files go here.
function(zwe_add_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE wavecore Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

zwe_add_test(tst_version tst_version.cpp)
zwe_add_test(tst_model tst_model.cpp)
zwe_add_test(tst_sampling tst_sampling.cpp)
zwe_add_test(tst_documenttransform tst_documenttransform.cpp)
zwe_add_test(tst_segmentcontinuity tst_segmentcontinuity.cpp)
target_link_libraries(tst_segmentcontinuity PRIVATE waveui)
zwe_add_test(tst_expression tst_expression.cpp)
zwe_add_test(tst_points tst_points.cpp)

zwe_add_test(tst_zwjio tst_zwjio.cpp)
target_compile_definitions(tst_zwjio PRIVATE
    ZWE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

zwe_add_test(tst_csvio tst_csvio.cpp)

zwe_add_test(tst_binaryio tst_binaryio.cpp)

zwe_add_test(tst_engineeringedit tst_engineeringedit.cpp)
target_link_libraries(tst_engineeringedit PRIVATE waveui)

zwe_add_test(tst_propertypanel tst_propertypanel.cpp)
target_link_libraries(tst_propertypanel PRIVATE waveui)
set_tests_properties(tst_propertypanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_commands tst_commands.cpp)
target_link_libraries(tst_commands PRIVATE waveui)

zwe_add_test(tst_structurepanel tst_structurepanel.cpp)
target_link_libraries(tst_structurepanel PRIVATE waveui)
set_tests_properties(tst_structurepanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_csvdialogs tst_csvdialogs.cpp)
target_link_libraries(tst_csvdialogs PRIVATE waveui)
set_tests_properties(tst_csvdialogs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_mainwindow tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE waveui)
target_compile_definitions(tst_mainwindow PRIVATE
    ZWE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
set_tests_properties(tst_mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_theme tst_theme.cpp)
target_link_libraries(tst_theme PRIVATE waveui)
set_tests_properties(tst_theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_settings tst_settings.cpp)
target_link_libraries(tst_settings PRIVATE waveui)
set_tests_properties(tst_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_help tst_help.cpp)
target_link_libraries(tst_help PRIVATE waveui)
set_tests_properties(tst_help PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_canvaswidget tst_canvaswidget.cpp)
target_link_libraries(tst_canvaswidget PRIVATE waveui)
set_tests_properties(tst_canvaswidget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

zwe_add_test(tst_gridbar tst_gridbar.cpp)
target_link_libraries(tst_gridbar PRIVATE waveui)
set_tests_properties(tst_gridbar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Disabled: the test drives the language from LANG/LC_ALL, but an explicitly
# stored "application/language" wins over the environment by design - so the
# test fails on any machine whose developer once picked a language in
# Edit > Settings, which says nothing about the catalogs. Re-enable it together
# with a way to run the editor without the stored setting.
# add_test(NAME tst_localization COMMAND $<TARGET_FILE:ZahnerWaveEditor> --help)
# set_tests_properties(tst_localization PROPERTIES
#     ENVIRONMENT "LANG=de_DE.UTF-8;LC_ALL=de_DE.UTF-8"
#     PASS_REGULAR_EXPRESSION "Wellenform-Editor"
# )
