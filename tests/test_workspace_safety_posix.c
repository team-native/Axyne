#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/features/workspace/workspace_safety.h"

/* Report to stdout so the historical stderr-closing bug remains diagnosable. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void)
{
    char temporary[] = "/tmp/axyne-nofollow-XXXXXX";
    char alias[1024], missing[1024];
    struct stat before[3], after;
    int flags[3];
    AxyneError error = {0};
    AxyneDirectoryList list = {0};
    CHECK(mkdtemp(temporary) != NULL);
    char *root = realpath(temporary, NULL);
    CHECK(root != NULL);
    CHECK(snprintf(alias, sizeof(alias), "%s/alias", root) < (int)sizeof(alias));
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", root) < (int)sizeof(missing));
    CHECK(symlink(root, alias) == 0);
    for (int descriptor = 0; descriptor < 3; ++descriptor) {
        CHECK(fstat(descriptor, &before[descriptor]) == 0);
        flags[descriptor] = fcntl(descriptor, F_GETFD);
        CHECK(flags[descriptor] >= 0);
    }

    /* Exercise the real caller that used to interpret status 2 as stderr's FD. */
    CHECK(axyne_fs_list_directory(alias, &list, &error) != AXYNE_STATUS_OK);
    axyne_fs_free_directory_list(&list);
    for (int descriptor = 0; descriptor < 3; ++descriptor) {
        CHECK(fstat(descriptor, &after) == 0);
        CHECK(after.st_dev == before[descriptor].st_dev &&
              after.st_ino == before[descriptor].st_ino);
        CHECK(fcntl(descriptor, F_GETFD) == flags[descriptor]);
    }
    errno = 0;
    CHECK(axyne_workspace_open_directory_nofollow(alias, &error) == -1);
    CHECK(errno == ELOOP || errno == ENOTDIR);
    CHECK(error.code != AXYNE_STATUS_OK && error.message[0] != '\0');
    CHECK(axyne_workspace_open_directory_nofollow(missing, &error) == -1);
    CHECK(errno == ENOENT && error.code == AXYNE_STATUS_NOT_FOUND);
    int directory = axyne_workspace_open_directory_nofollow(root, &error);
    CHECK(directory >= 0);
    CHECK(close(directory) == 0);
    CHECK(unlink(alias) == 0);
    CHECK(rmdir(root) == 0);
    free(root);
    puts("No-follow symlink/missing-path failure preserves errno and standard descriptors");
    return EXIT_SUCCESS;
}
