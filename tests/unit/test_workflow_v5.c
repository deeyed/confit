#include <stdlib.h>
#include <string.h>

#include "confit/config_v5.h"
#include "confit/diagnostic.h"
#include "confit/status.h"
#include "confit/workflow_v5.h"
#include "test_assert.h"

static ConfitV5Workflow *open_production(void) {
  char repository[4096];
  ConfitV5CatalogRequest request;
  ConfitV5Workflow *workflow = 0;
  ConfitDiagnostic diagnostic;
  CONFIT_TEST_ASSERT(realpath(CONFIT_TEST_SOURCE_DIR "/../..", repository) !=
                     0);
  request.repository_root = repository;
  request.architecture = "arm64";
  request.kernconf = "qemu_virt_dev";
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_workflow_open(&request, &workflow,
                                             &diagnostic) == CONFIT_OK);
  return workflow;
}

static void expect_shared_model_and_minimal_output(void) {
  ConfitV5Workflow *left = open_production();
  ConfitV5Workflow *right = open_production();
  ConfitDiagnostic diagnostic;
  char *left_text = 0;
  char *right_text = 0;
  size_t left_size = 0U;
  size_t right_size = 0U;
  size_t match = 0U;
  ConfitV5WorkflowRow row;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_workflow_row_count(left) == 7U);
  CONFIT_TEST_ASSERT(
      confit_v5_workflow_search(left, "gacs", 0U, &match));
  CONFIT_TEST_ASSERT(confit_v5_workflow_row(left, match, &row));
  CONFIT_TEST_ASSERT(strcmp(row.option.symbol, "DRIVER_GACS_QEMU_MOCK") == 0);
  CONFIT_TEST_ASSERT(row.origin == CONFIT_V5_VALUE_ORIGIN_USER);
  CONFIT_TEST_ASSERT(confit_v5_workflow_minimal(
                         left, &left_text, &left_size, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v5_workflow_minimal(right, &right_text, &right_size,
                                                &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(left_size == right_size);
  CONFIT_TEST_ASSERT(memcmp(left_text, right_text, left_size) == 0);
  CONFIT_TEST_ASSERT(strstr(left_text, "MACHINE_QEMU_VIRT") != 0);
  CONFIT_TEST_ASSERT(strstr(left_text, "MACHINE_RPI5") == 0);
  free(right_text);
  free(left_text);
  confit_v5_workflow_free(right);
  confit_v5_workflow_free(left);
}

static void expect_detail_and_edit_share_reason_graph(void) {
  ConfitV5Workflow *workflow = open_production();
  ConfitDiagnostic diagnostic;
  char *detail = 0;
  char *minimal = 0;
  size_t size = 0U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_workflow_explain(
                         workflow, "DRIVER_IOMMU_SMMUV3", 0, &detail, &size,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strstr(detail, "origin=user") != 0);
  CONFIT_TEST_ASSERT(strstr(detail, "reason=request:satisfied:kernel") != 0);
  free(detail);
  CONFIT_TEST_ASSERT(confit_v5_workflow_set(
                         workflow, "DRIVER_GACS_QEMU_MOCK", "off",
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_v5_workflow_minimal(
                         workflow, &minimal, &size, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strstr(minimal, "DRIVER_GACS_QEMU_MOCK") == 0);
  free(minimal);
  CONFIT_TEST_ASSERT(confit_v5_workflow_set(
                         workflow, "REMOVED_OR_TYPO", "true",
                         &diagnostic) == CONFIT_ERR_SCHEMA);
  confit_v5_workflow_free(workflow);
}

static void expect_bounded_accessible_rendering(void) {
  ConfitV5Workflow *workflow = open_production();
  ConfitDiagnostic diagnostic;
  char *screen = 0;
  size_t size = 0U;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_workflow_render(
                         workflow, 0U, "", 80U, 24U, &screen, &size,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(strstr(screen, "ARCH=arm64") != 0);
  CONFIT_TEST_ASSERT(strstr(screen, "Origin") != 0);
  CONFIT_TEST_ASSERT(strstr(screen, "Keys: arrows/jk") != 0);
  free(screen);
  CONFIT_TEST_ASSERT(confit_v5_workflow_render(
                         workflow, 0U, "", 39U, 24U, &screen, &size,
                         &diagnostic) == CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(confit_v5_workflow_render(
                         workflow, 0U, "", 80U, 9U, &screen, &size,
                         &diagnostic) == CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(confit_v5_workflow_render(
                         workflow, 0U, "", 513U, 24U, &screen, &size,
                         &diagnostic) == CONFIT_ERR_INVALID_ARGUMENT);
  confit_v5_workflow_free(workflow);
}

static void expect_closed_input_decoder(void) {
  ConfitV5TuiAction action;
  ConfitDiagnostic diagnostic;
  char oversized[140];
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_v5_tui_decode("\033", &action, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(action.kind == CONFIT_V5_TUI_ACTION_CANCEL);
  CONFIT_TEST_ASSERT(confit_v5_tui_decode("\033[A", &action, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(action.kind == CONFIT_V5_TUI_ACTION_UP);
  CONFIT_TEST_ASSERT(confit_v5_tui_decode("/iommu", &action, &diagnostic) ==
                     CONFIT_OK);
  CONFIT_TEST_ASSERT(action.kind == CONFIT_V5_TUI_ACTION_SEARCH);
  CONFIT_TEST_ASSERT(strcmp(action.value, "iommu") == 0);
  CONFIT_TEST_ASSERT(confit_v5_tui_decode(
                         "set DRIVER_IOMMU_SMMUV3 off", &action,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(action.kind == CONFIT_V5_TUI_ACTION_SET);
  CONFIT_TEST_ASSERT(strcmp(action.symbol, "DRIVER_IOMMU_SMMUV3") == 0);
  memset(oversized, 'a', sizeof(oversized));
  oversized[0] = '/';
  oversized[sizeof(oversized) - 1U] = '\0';
  CONFIT_TEST_ASSERT(confit_v5_tui_decode(oversized, &action, &diagnostic) ==
                     CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(confit_v5_tui_decode("\033[999~", &action, &diagnostic) ==
                     CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(confit_v5_tui_decode("set BAD-NAME true", &action,
                                          &diagnostic) ==
                     CONFIT_ERR_INVALID_ARGUMENT);
}

int main(void) {
  expect_shared_model_and_minimal_output();
  expect_detail_and_edit_share_reason_graph();
  expect_bounded_accessible_rendering();
  expect_closed_input_decoder();
  return 0;
}
