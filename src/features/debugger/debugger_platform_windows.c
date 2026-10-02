#ifdef _WIN32
#include "axyne/debugger.h"

AxyneStatus axyne_debugger_configure_default(AxyneDebugger *debugger,
                                             AxyneError *error)
{
    static const char *arguments[] = {"--interpreter=mi2"};
    static const AxyneRunnerSpec spec = {
        "gdb.exe", arguments, 1, NULL, NULL, 0, 0, 0
    };
    return axyne_debugger_configure(debugger, &spec,
        AXYNE_DEBUGGER_PROTOCOL_MI2, error);
}
#endif
