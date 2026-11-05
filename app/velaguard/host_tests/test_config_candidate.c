#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vg_config_candidate.h"

static const char *good_candidate =
  "{"
  "\"candidate_schema_version\":1,"
  "\"mode\":\"add_sensor\","
  "\"bus\":{\"baud\":9600},"
  "\"sensor\":{\"slave_address\":1,\"model\":\"TH-01\"},"
  "\"manual\":{\"manual_id\":\"manual-01\","
  "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
  "\"evidence\":[{\"id\":\"ev1\",\"page\":12,"
  "\"quote\":\"Temp register 0x0000, int16, scale 0.1, unit C.\"}],"
  "\"points\":[{\"id\":\"temp\",\"name\":\"Temperature\",\"addr\":1,"
  "\"fc\":3,\"reg\":0,\"qty\":1,\"dtype\":\"int16\",\"scale\":0.1,"
  "\"offset\":0,\"unit\":\"C\","
  "\"source\":{\"evidence_ids\":[\"ev1\"],"
  "\"original_address\":\"0x0000\",\"address_base\":0}}]"
  "}";

static void empty_table(struct vg_discover_summary *table)
{
  memset(table, 0, sizeof(*table));
  table->baud = 9600;
  strcpy(table->devpath, "/dev/rs485");
}

static int build_with(const char *json, struct vg_config_candidate_build *build)
{
  struct vg_discover_summary current;

  empty_table(&current);
  return vg_config_candidate_build_json(json, strlen(json), &current, build);
}

static void replace_once(const char *input, const char *needle,
                         const char *replacement,
                         char *out, size_t capacity)
{
  const char *found = strstr(input, needle);
  size_t prefix;
  size_t suffix_length;

  assert(found != NULL);
  prefix = (size_t)(found - input);
  suffix_length = strlen(found + strlen(needle));
  assert(prefix + strlen(replacement) + suffix_length + 1 <= capacity);
  memcpy(out, input, prefix);
  memcpy(out + prefix, replacement, strlen(replacement));
  memcpy(out + prefix + strlen(replacement), found + strlen(needle),
         suffix_length + 1);
}

int main(void)
{
  static const char duplicate[] =
    "{\"candidate_schema_version\":1,\"candidate_schema_version\":1}";
  static const char unknown[] =
    "{\"candidate_schema_version\":1,\"unknown\":1}";
  static const uint8_t invalid_utf8[] =
    "{\"x\":\"\xff\"}";
  static const char unpaired[] =
    "{\"x\":\"\\ud800\"}";
  static const uint8_t nul_escape[] =
    "{\"x\":\"\\u0000\"}";
  static const char nested_duplicate[] =
    "{\"outer\":{\"same\":1,\"same\":2}}";
  struct vg_config_candidate_build build;
  struct vg_config_candidate_build deterministic;
  struct vg_discover_summary current;
  char modified[4096];
  char *limit_json;
  int i;

  assert(vg_config_json_strict_validate(good_candidate,
                                        strlen(good_candidate)) == 0);
  assert(vg_config_json_strict_validate(duplicate,
                                        strlen(duplicate)) == -EEXIST);
  assert(vg_config_json_strict_validate(nested_duplicate,
                                        strlen(nested_duplicate)) == -EEXIST);
  assert(vg_config_json_strict_validate(unknown,
                                        strlen(unknown)) == 0);
  assert(vg_config_json_strict_validate(invalid_utf8,
                                        sizeof(invalid_utf8) - 1) == -EINVAL);
  assert(vg_config_json_strict_validate(unpaired,
                                        strlen(unpaired)) == -EINVAL);
  assert(vg_config_json_strict_validate(nul_escape,
                                        sizeof(nul_escape) - 1) == -EINVAL);
  limit_json = malloc(VG_CONFIG_REQUEST_MAX + 1);
  assert(limit_json != NULL);
  memset(limit_json, ' ', VG_CONFIG_REQUEST_MAX + 1);
  limit_json[VG_CONFIG_REQUEST_MAX - 2] = '{';
  limit_json[VG_CONFIG_REQUEST_MAX - 1] = '}';
  assert(vg_config_json_strict_validate(limit_json,
                                        VG_CONFIG_REQUEST_MAX) == 0);
  assert(vg_config_json_strict_validate(limit_json,
                                        VG_CONFIG_REQUEST_MAX + 1) == -EINVAL);
  free(limit_json);

  assert(build_with(good_candidate, &build) == 0);
  assert(build.candidate.schema_version == 1);
  assert(strcmp(build.candidate.mode, "add_sensor") == 0);
  assert(build.candidate.slave_address == 1);
  assert(build.candidate.evidence_count == 1);
  assert(build.candidate.point_count == 1);
  assert(build.merged.n_points == 1);
  assert(strcmp(build.merged.points[0].id, "temp") == 0);
  assert(build.merged.points[0].reg == 0);
  assert(build.merged.points[0].scale > 0.09f);
  assert(build.target_len > 0);
  assert(build.target_json[0] == '{');
  assert(strlen(build.candidate_hash) == 64);
  assert(strstr(build.target_json, "\"id\":\"temp\"") != NULL);
  memset(&deterministic, 0xa5, sizeof(deterministic));
  assert(build_with(good_candidate, &deterministic) == 0);
  assert(deterministic.target_len == build.target_len);
  assert(strcmp(deterministic.target_json, build.target_json) == 0);
  assert(strcmp(deterministic.candidate_hash, build.candidate_hash) == 0);

  strcpy(modified, good_candidate);
  assert(build_with("{", &build) == -EINVAL);
  assert(build_with("{\"candidate_schema_version\":1,\"mode\":\"add_sensor\"}",
                    &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"unit\":\"C\"");
    assert(field != NULL);
    memcpy(field, "\"unit\":\"C\",\"extra\":1", 21);
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"candidate_schema_version\":1");
    assert(field != NULL);
    memcpy(field, "\"candidate_schema_versioX\":1",
           strlen("\"candidate_schema_versioX\":1"));
  }
  assert(build_with(modified, &build) == -EINVAL);

  {
    replace_once(good_candidate, "\"model\":\"TH-01\"",
                 "\"model\":\"TH\\n01\"", modified, sizeof(modified));
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"0x0000\"");
    assert(field != NULL);
    field[1] = 'X';
  }
  assert(build_with(modified, &build) == -EINVAL);

  {
    replace_once(good_candidate, "\"scale\":0.1",
                 "\"scale\":1e-50", modified, sizeof(modified));
  }
  assert(build_with(modified, &build) == -EINVAL);

  {
    replace_once(good_candidate,
                 "\"quote\":\"Temp register 0x0000, int16, scale 0.1, unit C.\"",
                 "\"quote\":\"wx\\u0085yz\"", modified, sizeof(modified));
  }
  assert(build_with(modified, &build) == -EINVAL);

  {
    replace_once(good_candidate, "\"unit\":\"C\"", "\"unit\":\"\"",
                 modified, sizeof(modified));
  }
  assert(build_with(modified, &build) == 0);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"reg\":0");
    assert(field != NULL);
    field[6] = '1';
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"qty\":1");
    assert(field != NULL);
    field[6] = '2';
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"offset\":0");
    assert(field != NULL);
    memcpy(field, "\"offset\":0.5", 12);
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "\"scale\":0.1");
    assert(field != NULL);
    memcpy(field, "\"scale\":0  ", 11);
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    char *field = strstr(modified, "[\"ev1\"]");
    assert(field != NULL);
    memcpy(field, "[\"ev2\"]", 7);
  }
  assert(build_with(modified, &build) == -EINVAL);

  strcpy(modified, good_candidate);
  {
    replace_once(good_candidate,
                 "\"evidence\":[{\"id\":\"ev1\",\"page\":12,"
                 "\"quote\":\"Temp register 0x0000, int16, scale 0.1, unit C.\"}]",
                 "\"evidence\":["
                 "{\"id\":\"ev1\",\"page\":12,\"quote\":\"first\"},"
                 "{\"id\":\"ev2\",\"page\":13,\"quote\":\"unused\"}]",
                 modified, sizeof(modified));
  }
  assert(build_with(modified, &build) == -EINVAL);
  assert(build_with(good_candidate, &build) == 0);
  empty_table(&current);
  strcpy(current.points[0].id, "external");
  strcpy(current.points[0].name, "external");
  strcpy(current.points[0].dtype, "int16");
  current.points[0].addr = 1;
  current.points[0].fc = 3;
  current.points[0].qty = 1;
  current.points[0].scale = 1;
  current.points[0].fail_n = 3;
  current.n_points = 1;
  assert(vg_config_candidate_build_json(good_candidate,
                                        strlen(good_candidate),
                                        &current, &build) == -EEXIST);

  empty_table(&current);
  current.n_points = VG_DISCOVER_MAX_POINTS - 1;
  for (i = 0; i < current.n_points; i++)
    {
      snprintf(current.points[i].id, sizeof(current.points[i].id),
               "p%d", i);
      strcpy(current.points[i].name, current.points[i].id);
      strcpy(current.points[i].dtype, "int16");
      current.points[i].addr = 3;
      current.points[i].fc = 3;
      current.points[i].reg = (uint16_t)i;
      current.points[i].qty = 1;
      current.points[i].scale = 1;
      current.points[i].fail_n = 3;
    }
  assert(vg_config_candidate_build_json(good_candidate,
                                        strlen(good_candidate),
                                        &current, &build) == 0);
  assert(build.merged.n_points == VG_DISCOVER_MAX_POINTS);

  current.n_points = VG_DISCOVER_MAX_POINTS;
  {
    struct vg_point_entry *point =
      &current.points[VG_DISCOVER_MAX_POINTS - 1];
    strcpy(point->id, "p31");
    strcpy(point->name, point->id);
    strcpy(point->dtype, "int16");
    point->addr = 3;
    point->fc = 3;
    point->reg = VG_DISCOVER_MAX_POINTS - 1;
    point->qty = 1;
    point->scale = 1;
    point->fail_n = 3;
  }
  assert(vg_config_candidate_build_json(good_candidate,
                                        strlen(good_candidate),
                                        &current, &build) == -E2BIG);
  return 0;
}
