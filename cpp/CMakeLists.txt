cmake_minimum_required(VERSION 3.16)
project(eq_tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
find_package(ftxui REQUIRED)

# Núcleo sem PipeWire: bandas, estado, presets, JSON e argumentos do filtro.
add_library(eq_base STATIC
    src/core/bands.cpp
    src/core/json.cpp
    src/core/state.cpp
    src/core/sys.cpp
)
target_include_directories(eq_base PUBLIC src/core)

# Cliente nativo do PipeWire e a convivência com a cadeia do Quickshell.
add_library(eq_core STATIC
    src/core/pw.cpp
    src/core/analyzer.cpp
    src/core/chain.cpp
)
target_link_libraries(eq_core PUBLIC eq_base PkgConfig::PIPEWIRE Threads::Threads)

add_library(eq_ui STATIC src/tui/ui.cpp)
target_include_directories(eq_ui PUBLIC src/tui)
target_link_libraries(eq_ui PUBLIC eq_core ftxui::component ftxui::dom ftxui::screen)

add_executable(eq-tui src/main.cpp)
target_link_libraries(eq-tui PRIVATE eq_ui)
target_compile_definitions(eq-tui PRIVATE EQ_TUI_VERSION="${PROJECT_VERSION}")

foreach(t eq_base eq_core eq_ui eq-tui)
    target_compile_options(${t} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE eq_core)
    add_test(NAME core COMMAND test_core)
    set_tests_properties(core PROPERTIES
        ENVIRONMENT "QSEQ_SHARE=${CMAKE_CURRENT_SOURCE_DIR}/../share/quickshell-eq")
endif()

install(TARGETS eq-tui RUNTIME DESTINATION bin)
