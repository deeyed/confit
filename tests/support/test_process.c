#include "test_process.h"

#include "test_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

void confit_test_process_result_clear(ConfitTestProcessResult *result) {
  if (result == 0) {
    return;
  }
  confit_test_fs_free(result->stdout_text);
  confit_test_fs_free(result->stderr_text);
  result->exit_code = -1;
  result->stdout_text = 0;
  result->stderr_text = 0;
}

#if defined(_WIN32)
static int confit_test_process_append_arg(char *out, size_t out_size,
                                          size_t *offset, const char *arg) {
  size_t index;

  if (*offset + 3U >= out_size) {
    return 0;
  }
  if (*offset > 0U) {
    out[*offset] = ' ';
    *offset += 1U;
  }
  out[*offset] = '"';
  *offset += 1U;
  for (index = 0U; arg[index] != '\0'; ++index) {
    if (*offset + 3U >= out_size) {
      return 0;
    }
    if (arg[index] == '"' || arg[index] == '\\') {
      out[*offset] = '\\';
      *offset += 1U;
    }
    out[*offset] = arg[index];
    *offset += 1U;
  }
  out[*offset] = '"';
  *offset += 1U;
  out[*offset] = '\0';
  return 1;
}

static int confit_test_process_make_command_line(const char *const *argv,
                                                 char *out, size_t out_size) {
  size_t offset;
  size_t index;

  if (argv == 0 || argv[0] == 0 || out == 0 || out_size == 0U) {
    return 0;
  }

  offset = 0U;
  out[0] = '\0';
  for (index = 0U; argv[index] != 0; ++index) {
    if (!confit_test_process_append_arg(out, out_size, &offset, argv[index])) {
      return 0;
    }
  }
  return 1;
}

int confit_test_process_run(const char *const *argv,
                            const char *working_directory,
                            const char *stdout_path,
                            const char *stderr_path,
                            ConfitTestProcessResult *result) {
  char command_line[8192];
  SECURITY_ATTRIBUTES security;
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  HANDLE stdin_file;
  HANDLE stdout_file;
  HANDLE stderr_file;
  DWORD exit_code;
  BOOL created;

  if (result == 0 || stdout_path == 0 || stderr_path == 0) {
    return 0;
  }
  result->exit_code = -1;
  result->stdout_text = 0;
  result->stderr_text = 0;

  if (!confit_test_process_make_command_line(argv, command_line,
                                             sizeof(command_line))) {
    return 0;
  }

  ZeroMemory(&security, sizeof(security));
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;

  stdin_file = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &security,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (stdin_file == INVALID_HANDLE_VALUE) {
    return 0;
  }
  stdout_file = CreateFileA(stdout_path, GENERIC_WRITE, FILE_SHARE_READ,
                            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            0);
  if (stdout_file == INVALID_HANDLE_VALUE) {
    CloseHandle(stdin_file);
    return 0;
  }
  stderr_file = CreateFileA(stderr_path, GENERIC_WRITE, FILE_SHARE_READ,
                            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            0);
  if (stderr_file == INVALID_HANDLE_VALUE) {
    CloseHandle(stdin_file);
    CloseHandle(stdout_file);
    return 0;
  }

  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = stdin_file;
  startup.hStdOutput = stdout_file;
  startup.hStdError = stderr_file;

  created = CreateProcessA(0, command_line, 0, 0, TRUE, 0, 0,
                           working_directory, &startup, &process);
  CloseHandle(stdin_file);
  CloseHandle(stdout_file);
  CloseHandle(stderr_file);
  if (!created) {
    return 0;
  }

  WaitForSingleObject(process.hProcess, INFINITE);
  if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  result->exit_code = (int)exit_code;
  result->stdout_text = confit_test_fs_read_file(stdout_path);
  result->stderr_text = confit_test_fs_read_file(stderr_path);
  return result->stdout_text != 0 && result->stderr_text != 0;
}
#else
int confit_test_process_run(const char *const *argv,
                            const char *working_directory,
                            const char *stdout_path,
                            const char *stderr_path,
                            ConfitTestProcessResult *result) {
  pid_t pid;
  int stdout_fd;
  int stderr_fd;
  int wait_status;

  if (result == 0 || argv == 0 || argv[0] == 0 || stdout_path == 0 ||
      stderr_path == 0) {
    return 0;
  }
  result->exit_code = -1;
  result->stdout_text = 0;
  result->stderr_text = 0;

  stdout_fd = open(stdout_path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
  if (stdout_fd < 0) {
    return 0;
  }
  stderr_fd = open(stderr_path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
  if (stderr_fd < 0) {
    close(stdout_fd);
    return 0;
  }

  pid = fork();
  if (pid < 0) {
    close(stdout_fd);
    close(stderr_fd);
    return 0;
  }

  if (pid == 0) {
    if (working_directory != 0 && working_directory[0] != '\0') {
      (void)chdir(working_directory);
    }
    (void)dup2(stdout_fd, STDOUT_FILENO);
    (void)dup2(stderr_fd, STDERR_FILENO);
    close(stdout_fd);
    close(stderr_fd);
    execv(argv[0], (char *const *)argv);
    _exit(127);
  }

  close(stdout_fd);
  close(stderr_fd);
  if (waitpid(pid, &wait_status, 0) < 0) {
    return 0;
  }

  if (WIFEXITED(wait_status)) {
    result->exit_code = WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    result->exit_code = 128 + WTERMSIG(wait_status);
  } else {
    result->exit_code = -1;
  }

  result->stdout_text = confit_test_fs_read_file(stdout_path);
  result->stderr_text = confit_test_fs_read_file(stderr_path);
  return result->stdout_text != 0 && result->stderr_text != 0;
}
#endif
