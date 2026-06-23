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

find_package(Curses REQUIRED)

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

if(MSVC)
  target_compile_options(confit_core PRIVATE /W4 /WX)
else()
  target_compile_options(confit_core PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_library(confit_host STATIC
  src/host/host_directory.c
  src/host/host_file.c
  src/host/host_io.c
  src/host/host_path.c
)

target_link_libraries(confit_host PUBLIC confit_core)
target_compile_features(confit_host PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_host PRIVATE /W4 /WX)
else()
  target_compile_options(confit_host PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

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

if(MSVC)
  target_compile_options(confit_parser PRIVATE /W4 /WX)
else()
  target_compile_options(confit_parser PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_library(confit_schema STATIC
  src/schema/schema.c
)

target_link_libraries(confit_schema PUBLIC confit_parser)
target_compile_features(confit_schema PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_schema PRIVATE /W4 /WX)
else()
  target_compile_options(confit_schema PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_library(confit_graph STATIC
  src/graph/graph.c
)

target_link_libraries(confit_graph PUBLIC confit_core)
target_compile_features(confit_graph PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_graph PRIVATE /W4 /WX)
else()
  target_compile_options(confit_graph PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_library(confit_resolver STATIC
  src/resolver/resolver.c
)

target_link_libraries(confit_resolver PUBLIC confit_core)
target_compile_features(confit_resolver PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_resolver PRIVATE /W4 /WX)
else()
  target_compile_options(confit_resolver PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_library(confit_explain STATIC
  src/explain/explain.c
)

target_link_libraries(confit_explain PUBLIC confit_resolver)
target_compile_features(confit_explain PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_explain PRIVATE /W4 /WX)
else()
  target_compile_options(confit_explain PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_library(confit_generator STATIC
  src/generator/build_integration.c
  src/generator/config_header.c
  src/generator/reports.c
)

target_link_libraries(confit_generator PUBLIC confit_resolver)
target_compile_features(confit_generator PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_generator PRIVATE /W4 /WX)
else()
  target_compile_options(confit_generator PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_library(confit_compat STATIC
  src/compat/compat.c
)

target_link_libraries(confit_compat PUBLIC confit_schema)
target_compile_features(confit_compat PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_compat PRIVATE /W4 /WX)
else()
  target_compile_options(confit_compat PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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
target_compile_features(confit_tui PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_tui PRIVATE /W4 /WX)
else()
  target_compile_options(confit_tui PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_executable(confit
  src/cli/main.c
)

target_link_libraries(confit PRIVATE
  confit_compat
  confit_explain
  confit_generator
  confit_tui
)

target_compile_definitions(confit PRIVATE
  CONFIT_BUILD_C_COMPILER_ID="${CMAKE_C_COMPILER_ID}"
  CONFIT_BUILD_C_COMPILER_VERSION="${CMAKE_C_COMPILER_VERSION}"
  CONFIT_BUILD_HAS_CURSES=1
  CONFIT_BUILD_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
)

if(MSVC)
  target_compile_options(confit PRIVATE /W4 /WX)
else()
  target_compile_options(confit PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

install(TARGETS confit RUNTIME DESTINATION bin)

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

if(MSVC)
  target_compile_options(confit_test_support PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_support PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_executable(confit_test_status_diagnostic
  tests/unit/test_status_diagnostic.c
)

target_link_libraries(confit_test_status_diagnostic PRIVATE confit_core)

if(MSVC)
  target_compile_options(confit_test_status_diagnostic PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_status_diagnostic PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_test(
  NAME confit.unit.status_diagnostic
  COMMAND confit_test_status_diagnostic
)

add_executable(confit_test_model
  tests/unit/test_model.c
)

target_link_libraries(confit_test_model PRIVATE confit_core)

if(MSVC)
  target_compile_options(confit_test_model PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_model PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_host_boundary PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_host_boundary PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_parser_adapter PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_parser_adapter PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_schema_loader PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_schema_loader PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_graph_builder PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_graph_builder PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_resolver PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_resolver PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_explain PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_explain PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_generator PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_generator PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_reports PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_reports PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

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

if(MSVC)
  target_compile_options(confit_test_compat PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_compat PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_test(
  NAME confit.unit.compat
  COMMAND confit_test_compat
)

add_test(
  NAME confit.cli.version
  COMMAND confit --version
)

set_tests_properties(confit.cli.version
  PROPERTIES PASS_REGULAR_EXPRESSION "confit 0\\.1\\.0-round1"
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

add_test(
  NAME confit.cli.workflow
  COMMAND /bin/sh
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round16_cli_workflow.sh
          $<TARGET_FILE:confit>
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/round16-cli
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

add_executable(confit_test_cli_workflow
  tests/integration_c/test_cli_workflow.c
)

target_link_libraries(confit_test_cli_workflow PRIVATE confit_test_support)
target_compile_definitions(confit_test_cli_workflow PRIVATE
  CONFIT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
  target_compile_options(confit_test_cli_workflow PRIVATE /W4 /WX)
else()
  target_compile_options(confit_test_cli_workflow PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_test(
  NAME confit.cli.workflow_c
  COMMAND confit_test_cli_workflow
          $<TARGET_FILE:confit>
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/round3-cli-c
)

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

add_test(
  NAME confit.cli.stress
  COMMAND /bin/sh
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/round20_stress.sh
          $<TARGET_FILE:confit>
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/round20-stress
)
