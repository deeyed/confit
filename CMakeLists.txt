cmake_minimum_required(VERSION 3.20)

project(
  confit
  VERSION 0.1.0
  DESCRIPTION "Confit host configuration tool"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC OR "${CMAKE_C_SIMULATE_ID}" STREQUAL "MSVC" OR
    "${CMAKE_C_COMPILER_FRONTEND_VARIANT}" STREQUAL "MSVC")
  message(FATAL_ERROR
    "Confit does not support MSVC or clang-cl. "
    "Use GNU-style clang for the Windows CLI-only lane."
  )
endif()

if(WIN32 AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR
    "Confit Windows support is CLI-only and clang-only. "
    "Use GNU-style clang, not MSVC, clang-cl, or MinGW GCC."
  )
endif()

option(CONFIT_ENABLE_TUI "Build the ncurses TUI frontend on supported hosts" ON)

if(WIN32)
  if(CONFIT_ENABLE_TUI)
    message(STATUS
      "Confit Windows CLI-only lane: forcing CONFIT_ENABLE_TUI=OFF")
  endif()
  set(CONFIT_ENABLE_TUI OFF CACHE BOOL
    "Build the ncurses TUI frontend on supported hosts" FORCE)
endif()

set(CONFIT_BUILD_HAS_CURSES_VALUE 0)
if(CONFIT_ENABLE_TUI)
  find_package(Curses REQUIRED)
  set(CONFIT_BUILD_HAS_CURSES_VALUE 1)
endif()

if(CMAKE_CONFIGURATION_TYPES)
  set(CONFIT_BUILD_MODE_VALUE "$<CONFIG>")
elseif(CMAKE_BUILD_TYPE)
  set(CONFIT_BUILD_MODE_VALUE "${CMAKE_BUILD_TYPE}")
else()
  set(CONFIT_BUILD_MODE_VALUE "unspecified")
endif()

if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wno-availability
    -Wno-extra-semi
    -Wno-expansion-to-defined
    -Wno-nullability-completeness
    -Wno-nullability-extension
  )
endif()

function(confit_target_warnings target_name)
  target_compile_options(${target_name} PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target_name} PRIVATE
      -Wno-format-truncation
    )
  endif()
endfunction()

add_library(confit_core STATIC
  src/core/diagnostic.c
  src/core/model.c
  src/core/status.c
  src/core/version.c
)

target_include_directories(confit_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(confit_core PUBLIC c_std_17)

confit_target_warnings(confit_core)

add_library(confit_host STATIC
  src/host/host_directory.c
  src/host/host_file.c
  src/host/host_io.c
  src/host/host_path.c
)

target_link_libraries(confit_host PUBLIC confit_core)
target_compile_features(confit_host PUBLIC c_std_17)

confit_target_warnings(confit_host)

add_library(confit_parser STATIC
  src/parser/parser.c
  src/parser/toml_scan.c
)

target_link_libraries(confit_parser PUBLIC confit_host)

target_include_directories(confit_parser
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
)

target_compile_features(confit_parser PUBLIC c_std_17)

confit_target_warnings(confit_parser)

add_library(confit_schema STATIC
  src/schema/schema.c
)

target_link_libraries(confit_schema PUBLIC confit_parser)
target_compile_features(confit_schema PUBLIC c_std_17)

confit_target_warnings(confit_schema)

add_library(confit_graph STATIC
  src/graph/graph.c
)

target_link_libraries(confit_graph PUBLIC confit_core)
target_compile_features(confit_graph PUBLIC c_std_17)

confit_target_warnings(confit_graph)

add_library(confit_resolver STATIC
  src/resolver/resolver.c
)

target_link_libraries(confit_resolver PUBLIC confit_core)
target_compile_features(confit_resolver PUBLIC c_std_17)

confit_target_warnings(confit_resolver)

add_library(confit_explain STATIC
  src/explain/explain.c
)

target_link_libraries(confit_explain PUBLIC confit_resolver)
target_compile_features(confit_explain PUBLIC c_std_17)

confit_target_warnings(confit_explain)

add_library(confit_generator STATIC
  src/generator/build_integration.c
  src/generator/config_header.c
  src/generator/reports.c
  src/generator/value_serialization.c
)

target_link_libraries(confit_generator PUBLIC confit_resolver)
target_compile_features(confit_generator PUBLIC c_std_17)

confit_target_warnings(confit_generator)

add_library(confit_compat STATIC
  src/compat/compat.c
)

target_link_libraries(confit_compat PUBLIC confit_schema)
target_compile_features(confit_compat PUBLIC c_std_17)

confit_target_warnings(confit_compat)

if(CONFIT_ENABLE_TUI)
  add_library(confit_tui STATIC
    src/tui/curses_frontend.c
    src/tui/profile_editor.c
    src/tui/schema_editor.c
    src/tui/tui.c
    src/tui/tui_common.c
  )

  target_link_libraries(confit_tui
    PUBLIC
      confit_schema
    PRIVATE
      confit_graph
      confit_resolver
      ${CURSES_LIBRARIES}
  )

  target_include_directories(confit_tui
    PRIVATE
      ${CURSES_INCLUDE_DIRS}
  )
else()
  add_library(confit_tui STATIC
    src/tui/tui_unsupported.c
  )

  target_link_libraries(confit_tui PUBLIC confit_core)
endif()

target_compile_features(confit_tui PUBLIC c_std_17)

confit_target_warnings(confit_tui)

add_executable(confit
  src/cli/main.c
)

target_link_libraries(confit PRIVATE
  confit_compat
  confit_explain
  confit_generator
  confit_tui
)

if(NOT CONFIT_ENABLE_TUI)
  target_link_libraries(confit PRIVATE confit_graph)
endif()

target_compile_definitions(confit PRIVATE
  CONFIT_BUILD_MODE="${CONFIT_BUILD_MODE_VALUE}"
  CONFIT_BUILD_C_COMPILER_ID="${CMAKE_C_COMPILER_ID}"
  CONFIT_BUILD_C_COMPILER_VERSION="${CMAKE_C_COMPILER_VERSION}"
  CONFIT_BUILD_HAS_CURSES=${CONFIT_BUILD_HAS_CURSES_VALUE}
  CONFIT_BUILD_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
)

confit_target_warnings(confit)

install(TARGETS confit RUNTIME DESTINATION bin)
install(FILES man/confit.1 DESTINATION share/man/man1)

enable_testing()

add_library(confit_test_support STATIC
  tests/support/test_assert.c
  tests/support/test_fs.c
  tests/support/test_process.c
)

target_include_directories(confit_test_support
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
)

target_compile_features(confit_test_support PUBLIC c_std_17)

confit_target_warnings(confit_test_support)

add_executable(confit_test_status_diagnostic
  tests/unit/test_status_diagnostic.c
)

target_link_libraries(confit_test_status_diagnostic PRIVATE confit_core)

confit_target_warnings(confit_test_status_diagnostic)

add_test(
  NAME confit.unit.status_diagnostic
  COMMAND confit_test_status_diagnostic
)

add_executable(confit_test_model
  tests/unit/test_model.c
)

target_link_libraries(confit_test_model PRIVATE confit_core)

confit_target_warnings(confit_test_model)

add_test(
  NAME confit.unit.model
  COMMAND confit_test_model
)

add_executable(confit_test_host_boundary
  tests/unit/test_host_boundary.c
)

target_link_libraries(confit_test_host_boundary PRIVATE confit_host)
target_compile_definitions(confit_test_host_boundary PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_host_boundary)

add_test(
  NAME confit.unit.host_boundary
  COMMAND confit_test_host_boundary
)

add_executable(confit_test_parser_adapter
  tests/unit/test_parser_adapter.c
)

target_link_libraries(confit_test_parser_adapter PRIVATE confit_parser)
target_compile_definitions(confit_test_parser_adapter PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_parser_adapter)

add_test(
  NAME confit.unit.parser_adapter
  COMMAND confit_test_parser_adapter
)

add_executable(confit_test_schema_loader
  tests/unit/test_schema_loader.c
)

target_link_libraries(confit_test_schema_loader PRIVATE confit_schema)
target_compile_definitions(confit_test_schema_loader PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_schema_loader)

add_test(
  NAME confit.unit.schema_loader
  COMMAND confit_test_schema_loader
)

add_executable(confit_test_graph_builder
  tests/unit/test_graph_builder.c
)

target_link_libraries(confit_test_graph_builder PRIVATE
  confit_graph
  confit_schema
)
target_compile_definitions(confit_test_graph_builder PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_graph_builder)

add_test(
  NAME confit.unit.graph_builder
  COMMAND confit_test_graph_builder
)

add_executable(confit_test_resolver
  tests/unit/test_resolver.c
)

target_link_libraries(confit_test_resolver PRIVATE
  confit_resolver
  confit_schema
)
target_compile_definitions(confit_test_resolver PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_resolver)

add_test(
  NAME confit.unit.resolver
  COMMAND confit_test_resolver
)

add_executable(confit_test_explain
  tests/unit/test_explain.c
)

target_link_libraries(confit_test_explain PRIVATE
  confit_explain
  confit_schema
)
target_compile_definitions(confit_test_explain PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_explain)

add_test(
  NAME confit.unit.explain
  COMMAND confit_test_explain
)

add_executable(confit_test_generator
  tests/unit/test_generator.c
)

target_link_libraries(confit_test_generator PRIVATE
  confit_generator
  confit_schema
)
target_compile_definitions(confit_test_generator PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_generator)

add_test(
  NAME confit.unit.generator
  COMMAND confit_test_generator
)

add_executable(confit_test_reports
  tests/unit/test_reports.c
)

target_link_libraries(confit_test_reports PRIVATE
  confit_generator
  confit_graph
  confit_schema
)
target_compile_definitions(confit_test_reports PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_reports)

add_test(
  NAME confit.unit.reports
  COMMAND confit_test_reports
)

add_executable(confit_test_compat
  tests/unit/test_compat.c
)

target_link_libraries(confit_test_compat PRIVATE
  confit_compat
  confit_resolver
)
target_compile_definitions(confit_test_compat PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

confit_target_warnings(confit_test_compat)

add_test(
  NAME confit.unit.compat
  COMMAND confit_test_compat
)

add_test(
  NAME confit.cli.version
  COMMAND confit --version
)

set_tests_properties(confit.cli.version
  PROPERTIES PASS_REGULAR_EXPRESSION "confit 0\\.1\\.0-rc1"
)

add_test(
  NAME confit.cli.help
  COMMAND confit help
)

set_tests_properties(confit.cli.help
  PROPERTIES PASS_REGULAR_EXPRESSION "Usage:"
)

add_test(
  NAME confit.cli.doctor
  COMMAND confit doctor
)

set_tests_properties(confit.cli.doctor
  PROPERTIES PASS_REGULAR_EXPRESSION "doctor ok"
)

add_test(
  NAME confit.cli.explain
  COMMAND confit explain --project
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/schema/valid/basic
          --profile sim-dsh delos.debug.ddc
)

set_tests_properties(confit.cli.explain
  PROPERTIES PASS_REGULAR_EXPRESSION "forced by:"
)

if(UNIX AND NOT WIN32)
  add_test(
    NAME confit.cli.workflow
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round16_cli_workflow.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round16-cli
  )

  add_test(
    NAME confit.cli.cutover_dry_run
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round16_cutover_dry_run.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round16-cutover
  )

  add_test(
    NAME confit.cli.realish_delos
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round11_realish_delos.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round11-realish-delos
  )

  add_test(
    NAME confit.cli.realish_parus
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round12_realish_parus.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round12-realish-parus
  )

  add_test(
    NAME confit.cli.realish_compat
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round13_realish_compat.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round13-realish-compat
  )

  add_test(
    NAME confit.cli.qstar_module_artifacts
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round8_qstar_module_artifacts.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round8-qstar-module-artifacts
  )
  set_tests_properties(confit.cli.qstar_module_artifacts
    PROPERTIES SKIP_RETURN_CODE 77
  )
endif()

add_executable(confit_test_cli_workflow
  tests/integration_c/test_cli_workflow.c
)

target_link_libraries(confit_test_cli_workflow PRIVATE confit_test_support)
target_compile_definitions(confit_test_cli_workflow PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  CONFIT_TEST_HAS_TUI=${CONFIT_BUILD_HAS_CURSES_VALUE}
)

confit_target_warnings(confit_test_cli_workflow)

add_test(
  NAME confit.cli.workflow_c
  COMMAND confit_test_cli_workflow
          $<TARGET_FILE:confit>
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/round4-cli-c
)

if(UNIX AND NOT WIN32 AND CONFIT_ENABLE_TUI)
  add_test(
    NAME confit.cli.tui
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round17_tui_smoke.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round17-tui
  )

  add_test(
    NAME confit.cli.tui_profile_editor
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round18_tui_profile_editor.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round18-tui
  )

  add_test(
    NAME confit.cli.tui_profile_create
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round19_tui_profile_create.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round19-profile-create
  )

  add_test(
    NAME confit.cli.tui_schema_editor
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round19_tui_schema_editor.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round19-schema-editor
  )
endif()

if(UNIX AND NOT WIN32)
  add_test(
    NAME confit.cli.cli_only_lane
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round2_cli_only_lane.sh
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round2-cli-only-lane
  )

  add_test(
    NAME confit.cli.stress
    COMMAND /bin/sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round20_stress.sh
            $<TARGET_FILE:confit>
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/round20-stress
  )
endif()
