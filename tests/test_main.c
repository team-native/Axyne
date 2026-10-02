#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int axyne_test_documents_search(const char *root);
int axyne_test_settings_preferences(const char *root);
int axyne_test_runner_git_lsp(const char *root, const char *source_root);
int axyne_test_process_stability(const char *root);
int axyne_test_palette_problems(const char *root);

static unsigned long axyne_test_process_id(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

int main(int argc, char **argv)
{
    char root[1024];
    int written;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <documents-search|settings-preferences|runner-git-lsp|process-stability|palette-problems> <fixture-root> [source-root]\n", argv[0]);
        return EXIT_FAILURE;
    }
    written = snprintf(root, sizeof(root), "%s-%lu", argv[2],
                       axyne_test_process_id());
    if (written < 0 || (size_t)written >= sizeof(root)) {
        fprintf(stderr, "fixture path is too long\n");
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "documents-search") == 0)
        return axyne_test_documents_search(root) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (strcmp(argv[1], "settings-preferences") == 0)
        return axyne_test_settings_preferences(root) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (strcmp(argv[1], "runner-git-lsp") == 0 && argc >= 4)
        return axyne_test_runner_git_lsp(root, argv[3]) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (strcmp(argv[1], "process-stability") == 0)
        return axyne_test_process_stability(root) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (strcmp(argv[1], "palette-problems") == 0)
        return axyne_test_palette_problems(root) ? EXIT_SUCCESS : EXIT_FAILURE;
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return EXIT_FAILURE;
}
