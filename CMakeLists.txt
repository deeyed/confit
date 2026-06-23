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

add_library(confit_toml_vendor STATIC
  vendor/toml/confit_toml.c
)

target_include_directories(confit_toml_vendor
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/toml
)

target_compile_features(confit_toml_vendor PUBLIC c_std_17)

if(MSVC)
  target_compile_options(confit_toml_vendor PRIVATE /W4 /WX)
else()
  target_compile_options(confit_toml_vendor PRIVATE
    -Wall -Wextra -Werror -pedantic
  )
endif()

add_library(confit_parser STATIC
  src/parser/parser.c
)

target_link_libraries(confit_parser
  PUBLIC
    confit_host
  PRIVATE
    confit_toml_vendor
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

add_executable(confit
  src/cli/main.c
)

target_link_libraries(confit PRIVATE confit_host)

if(MSVC)
  target_compile_options(confit PRIVATE /W4 /WX)
else()
  target_compile_options(confit PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

enable_testing()

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
