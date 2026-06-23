#ifndef CONFIT_TEST_FS_H
#define CONFIT_TEST_FS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char confit_test_fs_separator(void);
int confit_test_fs_path_join(char *out, size_t out_size, const char *left,
                             const char *right);
int confit_test_fs_make_dirs(const char *path);
int confit_test_fs_remove_tree(const char *path);
int confit_test_fs_file_exists(const char *path);
char *confit_test_fs_read_file(const char *path);
void confit_test_fs_free(void *allocation);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TEST_FS_H */
