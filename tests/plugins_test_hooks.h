#ifndef AXYNE_PLUGINS_TEST_HOOKS_H
#define AXYNE_PLUGINS_TEST_HOOKS_H

void axyne_plugins_test_set_rename_failure_mask(unsigned mask);
void axyne_plugins_test_arm_backup_collision(void);
const char *axyne_plugins_test_last_occupied_backup_path(void);
void axyne_plugins_test_reset(void);

#endif
