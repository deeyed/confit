#ifndef CONFIT_TEST_PROCESS_H
#define CONFIT_TEST_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ConfitTestProcessResult {
  int exit_code;
  char *stdout_text;
  char *stderr_text;
} ConfitTestProcessResult;

int confit_test_process_run(const char *const *argv,
                            const char *working_directory,
                            const char *stdout_path,
                            const char *stderr_path,
                            ConfitTestProcessResult *result);
void confit_test_process_result_clear(ConfitTestProcessResult *result);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TEST_PROCESS_H */
