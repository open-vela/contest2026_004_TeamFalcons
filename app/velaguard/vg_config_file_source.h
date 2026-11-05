/****************************************************************************
 * app/velaguard/vg_config_file_source.h
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_CONFIG_FILE_SOURCE_H
#define __APP_VELAGUARD_VG_CONFIG_FILE_SOURCE_H

#include "vg_config_service.h"

#define VG_CONFIG_SOURCE_PATH_MAX 192
#define VG_CONFIG_SOURCE_ROOT_MAX 96

struct vg_config_file_source
{
  char points_path[VG_CONFIG_SOURCE_PATH_MAX];
  char store_root[VG_CONFIG_SOURCE_ROOT_MAX];
};

int vg_config_file_source_init(struct vg_config_file_source *source,
                               const char *points_path,
                               const char *store_root);
int vg_config_file_source_load(void *ctx,
                               struct vg_config_table_snapshot *out);

#endif
