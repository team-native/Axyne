#ifndef AXYNE_APP_H
#define AXYNE_APP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneApp {
    const char *name;
    int initialized;
} AxyneApp;

int axyne_app_initialize(AxyneApp *app);
void axyne_app_shutdown(AxyneApp *app);
const char *axyne_app_name(void);
const char *axyne_app_version(void);
const char *axyne_app_author(void);

#ifdef __cplusplus
}
#endif

#endif
