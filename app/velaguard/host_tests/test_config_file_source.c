#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../vg_config_file_source.h"

static void remove_table(const char *path)
{
  char derived[256];

  unlink(path);
  snprintf(derived, sizeof(derived), "%s.bak", path);
  unlink(derived);
  snprintf(derived, sizeof(derived), "%s.tmp", path);
  unlink(derived);
  snprintf(derived, sizeof(derived), "%s.btmp", path);
  unlink(derived);
}

int main(void)
{
  const char *path = "config-source-points.json";
  struct vg_discover_summary table;
  struct vg_config_file_source source;
  struct vg_config_table_snapshot snapshot;
  FILE *fp;

  remove_table(path);
  memset(&table, 0, sizeof(table));
  table.baud = 9600;
  strcpy(table.devpath, "/dev/rs485");
  table.n_points = 2;
  strcpy(table.points[0].id, "temp");
  strcpy(table.points[0].name, "temp");
  strcpy(table.points[0].dtype, "int16");
  table.points[0].addr = 1;
  table.points[0].fc = 3;
  table.points[0].qty = 1;
  table.points[0].scale = 1;
  table.points[0].fail_n = 3;
  strcpy(table.points[1].id, "humidity");
  strcpy(table.points[1].name, "humidity");
  strcpy(table.points[1].dtype, "uint16");
  table.points[1].addr = 2;
  table.points[1].fc = 4;
  table.points[1].qty = 1;
  table.points[1].scale = 0.1f;
  table.points[1].fail_n = 3;
  assert(vg_point_table_write_candidate(&table, path) == 0);

  assert(vg_config_file_source_init(&source, path, ".") == 0);
  assert(vg_config_file_source_load(&source, &snapshot) == 0);
  assert(snapshot.state == VG_CONFIG_TABLE_VALID);
  assert(snapshot.point_count == 2);
  assert(strcmp(snapshot.point_ids[0], "temp") == 0);
  assert(strcmp(snapshot.point_ids[1], "humidity") == 0);
  assert(snapshot.slave_count == 2);
  assert(snapshot.slave_addresses[0] == 1);
  assert(snapshot.slave_addresses[1] == 2);
  assert(snapshot.has_base_hash);
  assert(strlen(snapshot.base_hash) == 64);

  memset(&table, 0, sizeof(table));
  table.baud = 9600;
  strcpy(table.devpath, "/dev/rs485");
  assert(vg_point_table_write_candidate(&table, path) == 0);
  assert(vg_config_file_source_load(&source, &snapshot) == 0);
  assert(snapshot.state == VG_CONFIG_TABLE_EMPTY);
  assert(snapshot.point_count == 0);
  assert(!snapshot.has_base_hash);

  remove_table(path);
  assert(vg_config_file_source_load(&source, &snapshot) == 0);
  assert(snapshot.state == VG_CONFIG_TABLE_EMPTY);
  assert(vg_config_file_source_init(&source, path,
                                    "missing-config-root") == 0);
  assert(vg_config_file_source_load(&source, &snapshot) == -ENOENT);
  assert(snapshot.state == VG_CONFIG_TABLE_UNAVAILABLE);

  fp = fopen(path, "w");
  assert(fp != NULL);
  fputs("{\"schema_version\":1,\"points\":[", fp);
  fclose(fp);
  assert(vg_config_file_source_init(&source, path, ".") == 0);
  assert(vg_config_file_source_load(&source, &snapshot) == -EINVAL);
  assert(snapshot.state == VG_CONFIG_TABLE_UNAVAILABLE);
  assert(vg_config_file_source_init(&source, NULL, ".") == -EINVAL);
  remove_table(path);
  return 0;
}
