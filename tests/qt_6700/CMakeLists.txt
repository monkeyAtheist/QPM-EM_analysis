cmake_minimum_required(VERSION 3.21)
project(QTsignalAppQt670Tests LANGUAGES CXX)

# Stand-alone test project. NOT part of QPM's production sources.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Test)
include(CTest)
get_filename_component(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(project_document_6700
    project_document_6700.cpp
    "${APP_ROOT}/src/antenna_project_document.cpp")
target_include_directories(project_document_6700 PRIVATE "${APP_ROOT}/include")
target_link_libraries(project_document_6700 PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(project_document_6700 PRIVATE
    QTA_FIXTURE_DIR="${APP_ROOT}/tests/fixtures")
add_test(NAME project_document_6700 COMMAND project_document_6700)

add_executable(logging_6700
    logging_6700.cpp
    "${APP_ROOT}/src/app_diagnostics.cpp")
target_include_directories(logging_6700 PRIVATE "${APP_ROOT}/include")
target_link_libraries(logging_6700 PRIVATE Qt6::Core Qt6::Test)
add_test(NAME logging_6700 COMMAND logging_6700)

add_executable(gui_smoke_6700
    gui_smoke_6700.cpp
    "${APP_ROOT}/src/em_calculator_widget.cpp"
    "${APP_ROOT}/src/em_calculator_model.cpp"
    "${APP_ROOT}/src/em_units_calculator_model.cpp"
    "${APP_ROOT}/src/laplace_rail_model.cpp"
    "${APP_ROOT}/src/laplace_rail_widget.cpp"
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(gui_smoke_6700 PRIVATE "${APP_ROOT}/include")
target_link_libraries(gui_smoke_6700 PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME gui_smoke_6700 COMMAND gui_smoke_6700)
set_tests_properties(gui_smoke_6700 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Surface Pen / mouse-ink persistence and undo/redo. Requires Qt Widgets on
# the Windows QPM kit; this executable is intentionally not part of production.
add_executable(ink_6800
    ink_6800.cpp
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(ink_6800 PRIVATE "${APP_ROOT}/include")
target_link_libraries(ink_6800 PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME ink_6800 COMMAND ink_6800)
set_tests_properties(ink_6800 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 6.9 multipage, typed text and legacy v1 ink compatibility.
add_executable(ink_6900
    ink_6900.cpp
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(ink_6900 PRIVATE "${APP_ROOT}/include")
target_link_libraries(ink_6900 PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME ink_6900 COMMAND ink_6900)
set_tests_properties(ink_6900 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 6.10 vector drawing tools + select/move + v2/v3 persistence.
add_executable(ink_61000
    ink_61000.cpp
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(ink_61000 PRIVATE "${APP_ROOT}/include")
target_link_libraries(ink_61000 PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME ink_61000 COMMAND ink_61000)
set_tests_properties(ink_61000 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 6.11 notebook viewport zoom/pan and embedded-image schema v4.
add_executable(ink_61100
    ink_61100.cpp
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(ink_61100 PRIVATE "${APP_ROOT}/include")
target_link_libraries(ink_61100 PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME ink_61100 COMMAND ink_61100)
set_tests_properties(ink_61100 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 6.12 electronic schematic vector symbols, migration v4->v5 and undo/redo.
add_executable(ink_61200
    ink_61200.cpp
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(ink_61200 PRIVATE "${APP_ROOT}/include")
target_link_libraries(ink_61200 PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME ink_61200 COMMAND ink_61200)
set_tests_properties(ink_61200 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 6.13: anchored orthogonal connector wires, group move, v5->v6 compatibility.
add_executable(ink_61300
    ink_61300.cpp
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(ink_61300 PRIVATE "${APP_ROOT}/include")
target_link_libraries(ink_61300 PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME ink_61300 COMMAND ink_61300)
set_tests_properties(ink_61300 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Independent physics acceptance against analytic Gauss-law reference.
add_executable(electrostatic_61300
    electrostatic_61300.cpp
    "${APP_ROOT}/src/electrostatic_model.cpp")
target_include_directories(electrostatic_61300 PRIVATE "${APP_ROOT}/include")
target_link_libraries(electrostatic_61300 PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME electrostatic_61300 COMMAND electrostatic_61300)

# 6.13.1: compact Notes toolbar, expandable advanced symbol/view tools.
add_executable(notes_layout_6131
    notes_layout_6131.cpp
    "${APP_ROOT}/src/em_calculator_widget.cpp"
    "${APP_ROOT}/src/em_calculator_model.cpp"
    "${APP_ROOT}/src/em_units_calculator_model.cpp"
    "${APP_ROOT}/src/laplace_rail_model.cpp"
    "${APP_ROOT}/src/laplace_rail_widget.cpp"
    "${APP_ROOT}/src/notes_ink_canvas.cpp")
target_include_directories(notes_layout_6131 PRIVATE "${APP_ROOT}/include")
target_link_libraries(notes_layout_6131 PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME notes_layout_6131 COMMAND notes_layout_6131)
set_tests_properties(notes_layout_6131 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 6.14: charge-density/total-charge conversion against the real electrostatic model.
add_executable(charge_density_61400
    charge_density_61400.cpp
    "${APP_ROOT}/src/electrostatic_model.cpp")
target_include_directories(charge_density_61400 PRIVATE "${APP_ROOT}/include")
target_link_libraries(charge_density_61400 PRIVATE Qt6::Core Qt6::Test)
add_test(NAME charge_density_61400 COMMAND charge_density_61400)
