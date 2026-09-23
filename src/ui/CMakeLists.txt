# waveui: main window, canvas, panels, dialogs. Links wavecore; the executable
# in src/app stays a thin shell around this library.
add_library(waveui STATIC
    editonlyserver.cpp
    editonlyserver.h
    ../../resources/waveeditor.qrc
    appsettings.cpp
    appsettings.h
    canvaswidget.cpp
    canvaswidget.h
    commands.cpp
    commands.h
    engineeringedit.cpp
    engineeringedit.h
    engineeringnotation.cpp
    engineeringnotation.h
    exportdialog.cpp
    exportdialog.h
    gridbar.cpp
    gridbar.h
    importdialog.cpp
    importdialog.h
    helpbrowser.cpp
    helpbrowser.h
    licenseviewerdialog.cpp
    licenseviewerdialog.h
    mainwindow.cpp
    mainwindow.h
    propertypanel.cpp
    propertypanel.h
    settingsdialog.cpp
    settingsdialog.h
    structurepanel.cpp
    structurepanel.h
    theme.cpp
    theme.h
)

target_include_directories(waveui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(waveui PUBLIC wavecore Qt6::Network Qt6::Widgets)
target_compile_definitions(waveui PRIVATE
    ZWE_HELP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../resources/help"
)
