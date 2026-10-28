#ifndef AXYNE_STARTUP_STEPS_H
#define AXYNE_STARTUP_STEPS_H

/* Startup loading screen model (Figma 7J8SYhLpybJgpxD3qFqL5u / 134:2451).
 * Both adapters show the same four steps in this order while the main
 * window is built, and draw the same geometry (logical pixels, centred in a
 * 1440x900 frame). Strings are UTF-8. */

typedef enum AxyneStartupStep {
    AXYNE_STARTUP_CORE = 0,   /* 코어 불러오기 */
    AXYNE_STARTUP_SETTINGS,   /* 설정 · 테마 읽기 */
    AXYNE_STARTUP_WORKSPACE,  /* 작업 공간 복원 */
    AXYNE_STARTUP_EDITOR,     /* 편집기 준비 */
    AXYNE_STARTUP_STEP_COUNT  /* not a step; also "all done" */
} AxyneStartupStep;

enum {
    AXYNE_STARTUP_LOGO = 76,
    AXYNE_STARTUP_TITLE_PT = 34,
    AXYNE_STARTUP_TRACK_WIDTH = 360,
    AXYNE_STARTUP_TRACK_HEIGHT = 6,
    AXYNE_STARTUP_STEP_ROW = 28,
    /* Vertical offsets from the top of the centred content block. */
    AXYNE_STARTUP_TITLE_Y = 94,
    AXYNE_STARTUP_TRACK_Y = 199,
    AXYNE_STARTUP_STATUS_Y = 223,
    AXYNE_STARTUP_STEPS_Y = 275,
    AXYNE_STARTUP_CONTENT_HEIGHT = 380
};

/* Figma colours, 0xRRGGBB. */
enum {
    AXYNE_STARTUP_BACKGROUND = 0x131419,
    AXYNE_STARTUP_TEXT = 0xf2f3f5,
    AXYNE_STARTUP_MUTED = 0x8b919b,
    AXYNE_STARTUP_DIM = 0x5a606a,
    AXYNE_STARTUP_ACCENT = 0xa974f4,
    AXYNE_STARTUP_TRACK = 0x2a2c33,
    AXYNE_STARTUP_DONE = 0xc9d68a
};

static inline const char *axyne_startup_step_label(AxyneStartupStep step)
{
    static const char *const labels[AXYNE_STARTUP_STEP_COUNT] = {
        "\xec\xbd\x94\xec\x96\xb4 \xeb\xb6\x88\xeb\x9f\xac\xec\x98\xa4\xea\xb8\xb0",
        "\xec\x84\xa4\xec\xa0\x95 \xc2\xb7 \xed\x85\x8c\xeb\xa7\x88 \xec\x9d\xbd\xea\xb8\xb0",
        "\xec\x9e\x91\xec\x97\x85 \xea\xb3\xb5\xea\xb0\x84 \xeb\xb3\xb5\xec\x9b\x90",
        "\xed\x8e\xb8\xec\xa7\x91\xea\xb8\xb0 \xec\xa4\x80\xeb\xb9\x84"
    };
    return (unsigned)step < AXYNE_STARTUP_STEP_COUNT ? labels[step] : "";
}

/* Status line under the bar while `step` runs, e.g. "작업 공간 복원 중…". */
static inline const char *axyne_startup_step_status(AxyneStartupStep step)
{
    static const char *const status[AXYNE_STARTUP_STEP_COUNT + 1] = {
        "\xec\xbd\x94\xec\x96\xb4 \xeb\xb6\x88\xeb\x9f\xac\xec\x98\xa4\xeb\x8a\x94 \xec\xa4\x91\xe2\x80\xa6",
        "\xec\x84\xa4\xec\xa0\x95 \xc2\xb7 \xed\x85\x8c\xeb\xa7\x88 \xec\x9d\xbd\xeb\x8a\x94 \xec\xa4\x91\xe2\x80\xa6",
        "\xec\x9e\x91\xec\x97\x85 \xea\xb3\xb5\xea\xb0\x84 \xeb\xb3\xb5\xec\x9b\x90 \xec\xa4\x91\xe2\x80\xa6",
        "\xed\x8e\xb8\xec\xa7\x91\xea\xb8\xb0 \xec\xa4\x80\xeb\xb9\x84 \xec\xa4\x91\xe2\x80\xa6",
        "\xec\xa4\x80\xeb\xb9\x84 \xec\x99\x84\xeb\xa3\x8c"
    };
    return (unsigned)step <= AXYNE_STARTUP_STEP_COUNT ? status[step] : "";
}

/* Bar fill while `step` runs: the finished steps' share, so it reaches 100
 * only when every step is done. */
static inline unsigned axyne_startup_progress(AxyneStartupStep step)
{
    if ((unsigned)step >= AXYNE_STARTUP_STEP_COUNT) return 100;
    return (unsigned)step * 100u / AXYNE_STARTUP_STEP_COUNT;
}

/* Footnote under the screen. */
#define AXYNE_STARTUP_FOOTNOTE \
    "\xec\xb2\xab \xec\x8b\xa4\xed\x96\x89\xec\x9d\x80 \xec\xa1\xb0\xea\xb8\x88 \xeb\x8d\x94 " \
    "\xea\xb1\xb8\xeb\xa6\xb4 \xec\x88\x98 \xec\x9e\x88\xec\x96\xb4\xec\x9a\x94"

#endif
