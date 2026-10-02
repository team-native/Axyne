#ifndef AXYNE_WORKSPACE_UTF8_H
#define AXYNE_WORKSPACE_UTF8_H

#include <stddef.h>
#include <stdint.h>

static inline int axyne_workspace_utf8_is_valid(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (p == 0) return 0;

    while (*p != 0) {
        uint32_t codepoint;
        size_t continuation;

        if (*p <= 0x7f) { ++p; continue; }
        if (*p >= 0xc2 && *p <= 0xdf) {
            codepoint = (uint32_t)(*p & 0x1f); continuation = 1;
        } else if (*p >= 0xe0 && *p <= 0xef) {
            codepoint = (uint32_t)(*p & 0x0f); continuation = 2;
        } else if (*p >= 0xf0 && *p <= 0xf4) {
            codepoint = (uint32_t)(*p & 0x07); continuation = 3;
        } else {
            return 0;
        }

        ++p;
        for (size_t i = 0; i < continuation; ++i) {
            if (p[i] == 0 || (p[i] & 0xc0) != 0x80) return 0;
            codepoint = (codepoint << 6) | (uint32_t)(p[i] & 0x3f);
        }
        if ((continuation == 1 && codepoint < 0x80) ||
            (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            codepoint > 0x10ffff) return 0;
        p += continuation;
    }
    return 1;
}

#endif
