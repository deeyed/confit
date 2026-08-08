#ifndef CONFIT_CLI_V2_WORKFLOW_H
#define CONFIT_CLI_V2_WORKFLOW_H

/**
 * @brief V2 source가 선택된 command만 처리한다.
 *
 * 이 module은 공개 CLI의 모든 command를 처리한다. v1 dispatcher나 legacy
 * command fallback은 존재하지 않으며, 지원하지 않는 요청은 caller가 fail-closed
 * diagnostic으로 끝낸다.
 */
int confit_cli_v2_try_run(const char *command, int argc, char **argv,
                          int *out_handled);

#endif /* CONFIT_CLI_V2_WORKFLOW_H */
