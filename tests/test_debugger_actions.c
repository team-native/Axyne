/* Clear-all-breakpoints helper over the real debugger breakpoint table. No
 * debugger process is started, so no gdb installation is required. */
#include <stdio.h>
#include <string.h>

#include "../src/features/ui/debugger_actions.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
    ++failures; } } while (0)

int main(void)
{
    AxyneDebugger debugger;
    AxyneError error;
    CHECK(axyne_debugger_initialize(&debugger, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_enabled_breakpoints(&debugger) == 0);
    CHECK(axyne_debugger_clear_breakpoints(&debugger, &error) == AXYNE_STATUS_OK);

    CHECK(axyne_debugger_toggle_breakpoint(&debugger, "/w/a.c", 3, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_toggle_breakpoint(&debugger, "/w/a.c", 9, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_toggle_breakpoint(&debugger, "/w/b.c", 1, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_toggle_breakpoint(&debugger, "/w/a.c", 9, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_enabled_breakpoints(&debugger) == 2);

    CHECK(axyne_debugger_clear_breakpoints(&debugger, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_enabled_breakpoints(&debugger) == 0);
    CHECK(debugger.breakpoint_count == 3);

    /* A cleared line can be set again, and clearing is idempotent. */
    CHECK(axyne_debugger_toggle_breakpoint(&debugger, "/w/a.c", 3, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_enabled_breakpoints(&debugger) == 1);
    CHECK(axyne_debugger_clear_breakpoints(&debugger, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_clear_breakpoints(&debugger, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_debugger_enabled_breakpoints(&debugger) == 0);

    /* Menu rules: a live session stays controllable whatever the document. */
    CHECK(axyne_debugger_can_control(1));
    CHECK(!axyne_debugger_can_control(0));
    CHECK(axyne_debugger_can_start(0, 0, 1));
    CHECK(!axyne_debugger_can_start(0, 0, 0));
    CHECK(!axyne_debugger_can_start(1, 0, 1));
    CHECK(!axyne_debugger_can_start(0, 1, 1));
    CHECK(axyne_debugger_can_toggle_breakpoint(1));
    CHECK(!axyne_debugger_can_toggle_breakpoint(0));

    axyne_debugger_destroy(&debugger);
    if (failures == 0) puts("debugger actions ok");
    return failures == 0 ? 0 : 1;
}
