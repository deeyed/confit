#include "test_fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

char confit_test_fs_separator(void) {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

static int confit_test_fs_is_separator(char value) {
  return value == '/' || value == '\\';
}

int confit_test_fs_path_join(char *out, size_t out_size, const char *left,
                             const char *right) {
  const size_t left_size = left != 0 ? strlen(left) : 0U;
  const size_t right_size = right != 0 ? strlen(right) : 0U;
  const int needs_separator =
      left_size > 0U && right_size > 0U &&
      !confit_test_fs_is_separator(left[left_size - 1U]);
  const size_t total_size =
      left_size + right_size + (needs_separator ? 1U : 0U);
  size_t offset;

  if (out == 0 || out_size == 0U || left == 0 || right == 0 ||
      total_size + 1U > out_size) {
    if (out != 0 && out_size > 0U) {
      out[0] = '\0';
    }
    return 0;
  }

  offset = 0U;
  memcpy(out + offset, left, left_size);
  offset += left_size;
  if (needs_separator) {
    out[offset] = confit_test_fs_separator();
    offset += 1U;
  }
  memcpy(out + offset, right, right_size);
  offset += right_size;
  out[offset] = '\0';
  return 1;
}

static int confit_test_fs_directory_exists(const char *path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
#else
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int confit_test_fs_create_dir_once(const char *path) {
  if (confit_test_fs_directory_exists(path)) {
    return 1;
  }
#if defined(_WIN32)
  if (CreateDirectoryA(path, 0) != 0) {
    return 1;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS &&
         confit_test_fs_directory_exists(path);
#else
  if (mkdir(path, 0777) == 0) {
    return 1;
  }
  return errno == EEXIST && confit_test_fs_directory_exists(path);
#endif
}

static int confit_test_fs_should_create_component(const char *path) {
  const size_t size = path != 0 ? strlen(path) : 0U;

  if (size == 0U) {
    return 0;
  }
  if (size == 2U && path[1] == ':') {
    return 0;
  }
  return 1;
}

int confit_test_fs_make_dirs(const char *path) {
  char *copy;
  size_t size;
  size_t index;

  if (path == 0 || path[0] == '\0') {
    return 0;
  }

  size = strlen(path);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, path, size + 1U);

  for (index = 1U; index < size; ++index) {
    char saved;

    if (!confit_test_fs_is_separator(copy[index])) {
      continue;
    }

    saved = copy[index];
    copy[index] = '\0';
    if (confit_test_fs_should_create_component(copy) &&
        !confit_test_fs_create_dir_once(copy)) {
      copy[index] = saved;
      free(copy);
      return 0;
    }
    copy[index] = saved;
  }

  if (!confit_test_fs_create_dir_once(path)) {
    free(copy);
    return 0;
  }

  free(copy);
  return 1;
}

int confit_test_fs_file_exists(const char *path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
#else
  struct stat info;
  return stat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

char *confit_test_fs_read_file(const char *path) {
  FILE *file;
  long length;
  char *buffer;
  size_t bytes_read;

  if (path == 0 || path[0] == '\0') {
    return 0;
  }

  file = fopen(path, "rb");
  if (file == 0) {
    return 0;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }
  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return 0;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }

  buffer = (char *)malloc((size_t)length + 1U);
  if (buffer == 0) {
    fclose(file);
    return 0;
  }
  bytes_read = fread(buffer, 1U, (size_t)length, file);
  if (bytes_read != (size_t)length) {
    free(buffer);
    fclose(file);
    return 0;
  }
  fclose(file);
  buffer[bytes_read] = '\0';
  return buffer;
}

#if defined(_WIN32)
static int confit_test_fs_remove_tree_windows(const char *path) {
  char pattern[MAX_PATH];
  WIN32_FIND_DATAA data;
  HANDLE handle;
  int ok;

  if (!confit_test_fs_path_join(pattern, sizeof(pattern), path, "*")) {
    return 0;
  }

  handle = FindFirstFileA(pattern, &data);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      char child[MAX_PATH];

      if (strcmp(data.cFileName, ".") == 0 ||
          strcmp(data.cFileName, "..") == 0) {
        continue;
      }
      if (!confit_test_fs_path_join(child, sizeof(child), path,
                                    data.cFileName)) {
        FindClose(handle);
        return 0;
      }
      if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        if (!confit_test_fs_remove_tree_windows(child)) {
          FindClose(handle);
          return 0;
        }
      } else if (DeleteFileA(child) == 0) {
        FindClose(handle);
        return 0;
      }
    } while (FindNextFileA(handle, &data) != 0);
    FindClose(handle);
  }

  ok = RemoveDirectoryA(path) != 0;
  return ok || GetLastError() == ERROR_FILE_NOT_FOUND ||
         GetLastError() == ERROR_PATH_NOT_FOUND;
}
#else
static int confit_test_fs_remove_tree_posix(const char *path) {
  DIR *dir;
  struct dirent *entry;

  dir = opendir(path);
  if (dir == 0) {
    return errno == ENOENT;
  }

  while ((entry = readdir(dir)) != 0) {
    char child[4096];
    struct stat info;

    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!confit_test_fs_path_join(child, sizeof(child), path, entry->d_name)) {
      closedir(dir);
      return 0;
    }
    if (lstat(child, &info) != 0) {
      closedir(dir);
      return 0;
    }
    if (S_ISDIR(info.st_mode)) {
      if (!confit_test_fs_remove_tree_posix(child)) {
        closedir(dir);
        return 0;
      }
    } else if (unlink(child) != 0) {
      closedir(dir);
      return 0;
    }
  }

  closedir(dir);
  return rmdir(path) == 0 || errno == ENOENT;
}
#endif

int confit_test_fs_remove_tree(const char *path) {
  if (path == 0 || path[0] == '\0') {
    return 0;
  }
#if defined(_WIN32)
  return confit_test_fs_remove_tree_windows(path);
#else
  return confit_test_fs_remove_tree_posix(path);
#endif
}

void confit_test_fs_free(void *allocation) { free(allocation); }
