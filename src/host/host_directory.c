#include "confit/host.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

static int confit_host_is_path_separator_local(char value) {
  return value == '/' || value == '\\';
}

static int confit_host_directory_exists(const char *path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
#else
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int confit_host_create_directory_once(const char *path) {
  if (confit_host_directory_exists(path)) {
    return 1;
  }

#if defined(_WIN32)
  if (CreateDirectoryA(path, 0) != 0) {
    return 1;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS &&
         confit_host_directory_exists(path);
#else
  if (mkdir(path, 0777) == 0) {
    return 1;
  }
  return errno == EEXIST && confit_host_directory_exists(path);
#endif
}

static int confit_host_should_create_component(const char *path) {
  const size_t size = strlen(path);

  if (size == 0U) {
    return 0;
  }
  if (size == 2U && path[1] == ':') {
    return 0;
  }
  return 1;
}

ConfitStatus confit_host_make_directories(const char *path,
                                          ConfitDiagnostic *diagnostic) {
  char *copy;
  size_t size;
  size_t index;

  if (path == 0 || path[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing directory path");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  size = strlen(path);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to allocate directory path");
    return CONFIT_ERR_INTERNAL;
  }
  memcpy(copy, path, size + 1U);

  for (index = 1U; index < size; ++index) {
    char saved;

    if (!confit_host_is_path_separator_local(copy[index])) {
      continue;
    }

    saved = copy[index];
    copy[index] = '\0';
    if (confit_host_should_create_component(copy) &&
        !confit_host_create_directory_once(copy)) {
      copy[index] = saved;
      free(copy);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                            "failed to create output directory");
      return CONFIT_ERR_GENERATION;
    }
    copy[index] = saved;
  }

  if (!confit_host_create_directory_once(path)) {
    free(copy);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "failed to create output directory");
    return CONFIT_ERR_GENERATION;
  }

  free(copy);
  return CONFIT_OK;
}

static int confit_host_has_toml_suffix(const char *name) {
  const size_t size = name != 0 ? strlen(name) : 0U;
  return size > 5U && strcmp(name + size - 5U, ".toml") == 0;
}

static char *confit_host_make_child_path(const char *directory,
                                         const char *name) {
  const size_t directory_size = strlen(directory);
  const size_t name_size = strlen(name);
  const int needs_separator =
      directory_size > 0U && directory[directory_size - 1U] != '/' &&
      directory[directory_size - 1U] != '\\';
  const size_t total_size =
      directory_size + name_size + (needs_separator ? 1U : 0U);
  char *path;
  size_t offset;

  path = (char *)malloc(total_size + 1U);
  if (path == 0) {
    return 0;
  }

  offset = 0U;
  memcpy(path + offset, directory, directory_size);
  offset += directory_size;
  if (needs_separator) {
    path[offset] = confit_host_path_separator();
    offset += 1U;
  }
  memcpy(path + offset, name, name_size);
  offset += name_size;
  path[offset] = '\0';
  return path;
}

static ConfitStatus confit_host_append_path(char ***items, size_t *item_count,
                                            const char *directory,
                                            const char *name,
                                            ConfitDiagnostic *diagnostic) {
  char **new_items;
  char *path;

  path = confit_host_make_child_path(directory, name);
  if (path == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, directory, 0, 0,
                          "failed to allocate directory entry path");
    return CONFIT_ERR_INTERNAL;
  }

  new_items =
      (char **)realloc(*items, (*item_count + 1U) * sizeof((*items)[0]));
  if (new_items == 0) {
    free(path);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, directory, 0, 0,
                          "failed to allocate directory entry list");
    return CONFIT_ERR_INTERNAL;
  }

  *items = new_items;
  (*items)[*item_count] = path;
  *item_count += 1U;
  return CONFIT_OK;
}

static int confit_host_compare_strings(const void *left, const void *right) {
  const char *const *left_string = (const char *const *)left;
  const char *const *right_string = (const char *const *)right;
  return strcmp(*left_string, *right_string);
}

static void confit_host_sort_strings(char **items, size_t item_count) {
  if (item_count > 1U) {
    qsort(items, item_count, sizeof(items[0]), confit_host_compare_strings);
  }
}

#if defined(_WIN32)
static ConfitStatus confit_host_list_toml_files_impl(
    const char *directory, char ***out_paths, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  WIN32_FIND_DATAA data;
  HANDLE handle;
  char *pattern;
  size_t directory_size;
  size_t pattern_size;

  directory_size = strlen(directory);
  pattern_size = directory_size + 8U;
  pattern = (char *)malloc(pattern_size);
  if (pattern == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, directory, 0, 0,
                          "failed to allocate directory search pattern");
    return CONFIT_ERR_INTERNAL;
  }

  if (directory_size > 0U &&
      (directory[directory_size - 1U] == '/' ||
       directory[directory_size - 1U] == '\\')) {
    memcpy(pattern, directory, directory_size);
    memcpy(pattern + directory_size, "*.toml", 7U);
  } else {
    memcpy(pattern, directory, directory_size);
    pattern[directory_size] = confit_host_path_separator();
    memcpy(pattern + directory_size + 1U, "*.toml", 7U);
  }

  handle = FindFirstFileA(pattern, &data);
  free(pattern);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();

    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return CONFIT_OK;
    }
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0, 0,
                          "failed to open directory");
    return CONFIT_ERR_PARSE;
  }

  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
        confit_host_has_toml_suffix(data.cFileName)) {
      ConfitStatus status = confit_host_append_path(
          out_paths, out_count, directory, data.cFileName, diagnostic);
      if (status != CONFIT_OK) {
        FindClose(handle);
        return status;
      }
    }
  } while (FindNextFileA(handle, &data) != 0);

  FindClose(handle);
  return CONFIT_OK;
}
#else
static ConfitStatus confit_host_list_toml_files_impl(
    const char *directory, char ***out_paths, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  DIR *dir;
  struct dirent *entry;

  errno = 0;
  dir = opendir(directory);
  if (dir == 0) {
    if (errno == ENOENT) {
      return CONFIT_OK;
    }
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0, 0,
                          "failed to open directory");
    return CONFIT_ERR_PARSE;
  }

  while ((entry = readdir(dir)) != 0) {
    ConfitStatus status;

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        !confit_host_has_toml_suffix(entry->d_name)) {
      continue;
    }

    status = confit_host_append_path(out_paths, out_count, directory,
                                     entry->d_name, diagnostic);
    if (status != CONFIT_OK) {
      closedir(dir);
      return status;
    }
  }

  if (closedir(dir) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, directory, 0, 0,
                          "failed to close directory");
    return CONFIT_ERR_PARSE;
  }

  return CONFIT_OK;
}
#endif

ConfitStatus confit_host_list_toml_files(const char *directory,
                                         char ***out_paths, size_t *out_count,
                                         ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  if (out_paths == 0 || out_count == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, directory, 0,
                          0, "missing directory list output");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_paths = 0;
  *out_count = 0U;
  if (directory == 0 || directory[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, directory, 0,
                          0, "missing directory path");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status =
      confit_host_list_toml_files_impl(directory, out_paths, out_count,
                                       diagnostic);
  if (status != CONFIT_OK) {
    confit_host_string_list_free(*out_paths, *out_count);
    *out_paths = 0;
    *out_count = 0U;
    return status;
  }

  confit_host_sort_strings(*out_paths, *out_count);
  return CONFIT_OK;
}

void confit_host_string_list_free(char **items, size_t count) {
  size_t index;

  for (index = 0U; index < count; ++index) {
    free(items[index]);
  }
  free(items);
}
