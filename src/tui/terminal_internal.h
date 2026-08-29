#ifndef CONFIT_TERMINAL_INTERNAL_H
#define CONFIT_TERMINAL_INTERNAL_H

#include "confit/diagnostic.h"
#include "confit/resolver.h"
#include "confit/status.h"
#include "confit/ui.h"

typedef ConfitStatus (*ConfitTerminalSaveFunction)(
    void *context, const ConfitResolution *resolution,
    ConfitDiagnostic *diagnostic);

typedef struct ConfitTerminalController {
  void *context;
  ConfitTerminalSaveFunction save;
} ConfitTerminalController;

/*
 * Run one ANSI-capable POSIX TTY session.  This layer owns terminal FDs and
 * terminal state only.  All project/snapshot I/O remains behind the callback.
 */
ConfitStatus confit_terminal_run(ConfitUiModel *model, int input_fd,
                                 int output_fd,
                                 const ConfitTerminalController *controller,
                                 ConfitDiagnostic *diagnostic);

#endif /* CONFIT_TERMINAL_INTERNAL_H */
