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

add_executable(confit
  src/cli/main.c
)

target_link_libraries(confit PRIVATE confit_core)

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
