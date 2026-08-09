#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "confit/host.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

static int confit_host_executable_name_valid(const char *name) {
  size_t index;

  if (name == 0 || name[0] == '\0' || strlen(name) > 128U) {
    return 0;
  }
  for (index = 0U; name[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)name[index];
    if (!(isalnum(value) || value == '.' || value == '_' || value == '+' ||
          value == '-')) {
      return 0;
    }
  }
  return 1;
}

ConfitStatus confit_host_resolve_executable(char *out, size_t out_size,
                                            const char *name,
                                            ConfitDiagnostic *diagnostic) {
  if (out == 0 || out_size == 0U ||
      !confit_host_executable_name_valid(name)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, name, 0U, 0U,
                          "invalid executable discovery name");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
#if defined(_WIN32)
  {
    char discovered[MAX_PATH];
    const DWORD size = SearchPathA(0, name, ".exe", (DWORD)sizeof(discovered),
                                   discovered, 0);
    if (size == 0U || size >= sizeof(discovered)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, name, 0U, 0U,
                            "required executable was not found");
      return CONFIT_ERR_UNSUPPORTED;
    }
    return confit_host_path_canonicalize(out, out_size, discovered, diagnostic);
  }
#else
  {
    const char *path = getenv("PATH");
    const char *cursor;
    if (path == 0 || path[0] == '\0') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, name, 0U, 0U,
                            "executable discovery PATH is empty");
      return CONFIT_ERR_UNSUPPORTED;
    }
    cursor = path;
    while (1) {
      const char *separator = strchr(cursor, ':');
      const size_t directory_size =
          separator != 0 ? (size_t)(separator - cursor) : strlen(cursor);
      char candidate[4096];
      int written;

      if (directory_size > 0U && directory_size < 3072U) {
        written = snprintf(candidate, sizeof(candidate), "%.*s/%s",
                           (int)directory_size, cursor, name);
        if (written > 0 && (size_t)written < sizeof(candidate) &&
            access(candidate, X_OK) == 0) {
          char directory[4096];
          char canonical_directory[4096];
          ConfitStatus status;
          if (directory_size + 1U > sizeof(directory)) {
            return CONFIT_ERR_INTERNAL;
          }
          memcpy(directory, cursor, directory_size);
          directory[directory_size] = '\0';
          status = confit_host_path_canonicalize(
              canonical_directory, sizeof(canonical_directory), directory,
              diagnostic);
          if (status == CONFIT_OK) {
            status = confit_host_path_join(out, out_size, canonical_directory,
                                           name, diagnostic);
          }
          /* LLVM의 multi-call driver는 argv[0] basename으로 ld.lld flavor를
           * 선택한다. Directory만 canonicalize하고 요청한 executable leaf를
           * 보존해야 canonical `lld`로 바뀌어 의미가 달라지지 않는다. */
          return status;
        }
      }
      if (separator == 0) {
        break;
      }
      cursor = separator + 1;
    }
  }
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, name, 0U, 0U,
                        "required executable was not found");
  return CONFIT_ERR_UNSUPPORTED;
#endif
}

ConfitStatus confit_host_capture_one_argument(
    char *out, size_t out_size, const char *executable, const char *argument,
    ConfitDiagnostic *diagnostic) {
  if (out == 0 || out_size < 2U || executable == 0 || executable[0] != '/' ||
      argument == 0 || argument[0] == '\0' || strlen(argument) > 128U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, executable,
                          0U, 0U, "invalid bounded tool probe argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  out[0] = '\0';
#if defined(_WIN32)
  (void)argument;
  confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, executable, 0U, 0U,
                        "bounded target tool probe is unavailable on this host");
  return CONFIT_ERR_UNSUPPORTED;
#else
  {
    int descriptors[2];
    posix_spawn_file_actions_t actions;
    pid_t child = 0;
    char *const arguments[] = {(char *)executable, (char *)argument, 0};
    size_t used = 0U;
    int status;
    int spawn_status;
    int wait_status;

    if (pipe(descriptors) != 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, executable, 0U, 0U,
                            "failed to create bounded tool probe pipe");
      return CONFIT_ERR_INTERNAL;
    }
    status = posix_spawn_file_actions_init(&actions);
    if (status == 0) status = posix_spawn_file_actions_adddup2(
        &actions, descriptors[1], STDOUT_FILENO);
    if (status == 0) status = posix_spawn_file_actions_addclose(
        &actions, descriptors[0]);
    if (status == 0) status = posix_spawn_file_actions_addclose(
        &actions, descriptors[1]);
    if (status != 0) {
      (void)posix_spawn_file_actions_destroy(&actions);
      (void)close(descriptors[0]);
      (void)close(descriptors[1]);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, executable, 0U, 0U,
                            "failed to prepare bounded tool probe");
      return CONFIT_ERR_INTERNAL;
    }
    spawn_status = posix_spawn(&child, executable, &actions, 0, arguments,
                               environ);
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(descriptors[1]);
    if (spawn_status != 0) {
      (void)close(descriptors[0]);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, executable, 0U,
                            0U, "failed to execute bounded target tool probe");
      return CONFIT_ERR_UNSUPPORTED;
    }
    while (1) {
      char buffer[256];
      const ssize_t count = read(descriptors[0], buffer, sizeof(buffer));
      if (count == 0) {
        break;
      }
      if (count < 0) {
        if (errno == EINTR) continue;
        used = out_size;
        break;
      }
      if ((size_t)count > out_size - 1U - used) {
        used = out_size;
        break;
      }
      memcpy(out + used, buffer, (size_t)count);
      used += (size_t)count;
    }
    (void)close(descriptors[0]);
    do {
      wait_status = waitpid(child, &status, 0);
    } while (wait_status < 0 && errno == EINTR);
    if (used >= out_size || wait_status != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
      out[0] = '\0';
      confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, executable, 0U,
                            0U, "bounded target tool probe failed or overflowed");
      return CONFIT_ERR_UNSUPPORTED;
    }
    while (used > 0U && (out[used - 1U] == '\n' || out[used - 1U] == '\r')) {
      used -= 1U;
    }
    out[used] = '\0';
    if (used == 0U || strchr(out, '\n') != 0 || strchr(out, '\r') != 0) {
      out[0] = '\0';
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, executable, 0U, 0U,
                            "target tool probe returned no single bounded line");
      return CONFIT_ERR_SCHEMA;
    }
    return CONFIT_OK;
  }
#endif
}
