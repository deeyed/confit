#ifndef CONFIT_TUI_H
#define CONFIT_TUI_H

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit TUI startup options다.
 */
typedef struct ConfitTuiOptions {
  /** project root path. */
  const char *project_root;
  /** profile name. */
  const char *profile_name;
  /** target name. 없으면 profile target을 사용한다. */
  const char *target_name;
} ConfitTuiOptions;

/**
 * @brief TUI profile editor를 실행한다.
 *
 * TUI는 project schema를 로드하고 resolved option list, search/filter,
 * type-aware editing, profile TOML 저장을 제공한다. 저장 전에는 resolver를
 * 통해 full validation을 다시 수행한다.
 *
 * @param options TUI startup options.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 정상 종료하면 CONFIT_OK.
 */
ConfitStatus confit_tui_run(const ConfitTuiOptions *options,
                            ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TUI_H */
