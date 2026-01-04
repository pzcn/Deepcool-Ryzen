#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmSnapshot {
    float ppt_power_w;
    float temp_c;
    float usage_pct;
    uint32_t flags;
} RmSnapshot;

__declspec(dllexport) int rm_init(void);
__declspec(dllexport) int rm_read(struct RmSnapshot* out);
__declspec(dllexport) void rm_shutdown(void);

#ifdef __cplusplus
}
#endif
