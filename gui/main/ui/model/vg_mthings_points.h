#ifndef VG_MTHINGS_POINTS_H
#define VG_MTHINGS_POINTS_H

#include <stdint.h>

typedef struct {
    uint8_t addr;
    uint16_t reg;
    uint8_t is_signed;
    float scale;
    const char *name;
    const char *unit;
    const char *dev;
} vg_mthings_point_t;

extern const vg_mthings_point_t vg_mthings_points[];
extern const int vg_mthings_point_count;

#endif
