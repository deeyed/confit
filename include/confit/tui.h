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
 * @brief TUI frontend skeleton을 실행한다.
 *
 * Round 17 skeleton은 project schema를 로드하고 resolved option list와 status
 * bar를 보여준다. 저장이나 schema/profile mutation은 이후 라운드에서 붙인다.
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
