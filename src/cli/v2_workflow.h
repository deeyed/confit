#ifndef CONFIT_CLI_V2_WORKFLOW_H
#define CONFIT_CLI_V2_WORKFLOW_H

/**
 * @brief V2 source가 선택된 command만 처리한다.
 *
 * V1 command는 `out_handled = 0`으로 남겨 legacy handler가 그대로 실행한다.
 * V2 command는 stdout/stderr/exit code contract를 이 module에서 완료한다.
 */
int confit_cli_v2_try_run(const char *command, int argc, char **argv,
                          int *out_handled);

#endif /* CONFIT_CLI_V2_WORKFLOW_H */
