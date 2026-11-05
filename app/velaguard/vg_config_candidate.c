/****************************************************************************
 * app/velaguard/vg_config_candidate.c
 ****************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netutils/cJSON.h>

#include "vg_config_candidate.h"
#include "../../boot/ota_core/vg_ota_sha256.h"

#define JSON_KEY_MAX 40
#define JSON_STRING_MAX 192
#define JSON_OBJECT_FIELDS_MAX 16
#define JSON_ARRAY_ITEMS_MAX 64
#define JSON_DEPTH_MAX 12

struct json_cursor
{
  const uint8_t *p;
  const uint8_t *end;
  int depth;
};

static void json_skip_ws(struct json_cursor *cursor)
{
  while (cursor->p < cursor->end)
    {
      uint8_t c = *cursor->p;
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
          break;
        }
      cursor->p++;
    }
}

static int hex_value(uint8_t c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int json_parse_hex4(struct json_cursor *cursor, uint32_t *out)
{
  uint32_t value = 0;
  int i;

  if ((size_t)(cursor->end - cursor->p) < 4)
    {
      return -EINVAL;
    }

  for (i = 0; i < 4; i++)
    {
      int digit = hex_value(cursor->p[i]);
      if (digit < 0)
        {
          return -EINVAL;
        }
      value = (value << 4) | (uint32_t)digit;
    }

  cursor->p += 4;
  *out = value;
  return 0;
}

static int utf8_codepoint(const uint8_t *p, size_t remaining,
                          uint32_t *out, size_t *used)
{
  uint32_t value;
  size_t length;
  uint32_t minimum;
  size_t i;

  if (remaining == 0)
    {
      return -EINVAL;
    }

  if (p[0] < 0x80)
    {
      value = p[0];
      length = 1;
      minimum = 0;
    }
  else if ((p[0] & 0xe0) == 0xc0)
    {
      value = p[0] & 0x1f;
      length = 2;
      minimum = 0x80;
    }
  else if ((p[0] & 0xf0) == 0xe0)
    {
      value = p[0] & 0x0f;
      length = 3;
      minimum = 0x800;
    }
  else if ((p[0] & 0xf8) == 0xf0)
    {
      value = p[0] & 0x07;
      length = 4;
      minimum = 0x10000;
    }
  else
    {
      return -EINVAL;
    }

  if (remaining < length)
    {
      return -EINVAL;
    }

  for (i = 1; i < length; i++)
    {
      if ((p[i] & 0xc0) != 0x80)
        {
          return -EINVAL;
        }
      value = (value << 6) | (p[i] & 0x3f);
    }

  if ((length > 1 && value < minimum) ||
      (value >= 0xd800 && value <= 0xdfff) ||
      value > 0x10ffff || value == 0)
    {
      return -EINVAL;
    }

  *out = value;
  *used = length;
  return 0;
}

static int append_codepoint(uint32_t value, char *out, size_t cap,
                            size_t *used)
{
  size_t length;

  if (value == 0)
    {
      return -EINVAL;
    }
  else if (value < 0x80)
    {
      length = 1;
    }
  else if (value < 0x800)
    {
      length = 2;
    }
  else if (value < 0x10000)
    {
      length = 3;
    }
  else
    {
      length = 4;
    }

  if (*used + length >= cap)
    {
      return -E2BIG;
    }

  switch (length)
    {
      case 1:
        out[(*used)++] = (char)value;
        break;
      case 2:
        out[(*used)++] = (char)(0xc0 | (value >> 6));
        out[(*used)++] = (char)(0x80 | (value & 0x3f));
        break;
      case 3:
        out[(*used)++] = (char)(0xe0 | (value >> 12));
        out[(*used)++] = (char)(0x80 | ((value >> 6) & 0x3f));
        out[(*used)++] = (char)(0x80 | (value & 0x3f));
        break;
      default:
        out[(*used)++] = (char)(0xf0 | (value >> 18));
        out[(*used)++] = (char)(0x80 | ((value >> 12) & 0x3f));
        out[(*used)++] = (char)(0x80 | ((value >> 6) & 0x3f));
        out[(*used)++] = (char)(0x80 | (value & 0x3f));
        break;
    }

  return 0;
}

static int json_parse_string(struct json_cursor *cursor,
                             char *out, size_t cap)
{
  size_t used = 0;

  if (out == NULL || cap == 0 || cursor->p >= cursor->end ||
      *cursor->p != '"')
    {
      return -EINVAL;
    }

  cursor->p++;
  while (cursor->p < cursor->end)
    {
      uint8_t c = *cursor->p++;
      if (c == '"')
        {
          out[used] = '\0';
          return 0;
        }
      if (c < 0x20)
        {
          return -EINVAL;
        }
      if (c == '\\')
        {
          uint32_t value;
          uint8_t escaped;

          if (cursor->p >= cursor->end)
            {
              return -EINVAL;
            }
          escaped = *cursor->p++;
          switch (escaped)
            {
              case '"': value = '"'; break;
              case '\\': value = '\\'; break;
              case '/': value = '/'; break;
              case 'b': value = '\b'; break;
              case 'f': value = '\f'; break;
              case 'n': value = '\n'; break;
              case 'r': value = '\r'; break;
              case 't': value = '\t'; break;
              case 'u':
                if (json_parse_hex4(cursor, &value) != 0)
                  {
                    return -EINVAL;
                  }
                if (value >= 0xd800 && value <= 0xdbff)
                  {
                    uint32_t low;
                    if ((size_t)(cursor->end - cursor->p) < 6 ||
                        cursor->p[0] != '\\' || cursor->p[1] != 'u')
                      {
                        return -EINVAL;
                      }
                    cursor->p += 2;
                    if (json_parse_hex4(cursor, &low) != 0 ||
                        low < 0xdc00 || low > 0xdfff)
                      {
                        return -EINVAL;
                      }
                    value = 0x10000 + ((value - 0xd800) << 10) +
                            (low - 0xdc00);
                  }
                else if (value >= 0xdc00 && value <= 0xdfff)
                  {
                    return -EINVAL;
                  }
                break;
              default:
                return -EINVAL;
            }

          {
            int append_result =
              append_codepoint(value, out, cap, &used);
            if (append_result != 0)
            {
              return append_result;
            }
          }
          continue;
        }

      if (c < 0x80)
        {
          if (used + 1 >= cap)
            {
              return -E2BIG;
            }
          out[used++] = (char)c;
        }
      else
        {
          size_t remaining = (size_t)(cursor->end - (cursor->p - 1));
          size_t length;
          uint32_t value;
          size_t i;

          if (utf8_codepoint(cursor->p - 1, remaining, &value, &length) != 0)
            {
              return -EINVAL;
            }
          if (used + length >= cap)
            {
              return -E2BIG;
            }
          for (i = 0; i < length; i++)
            {
              out[used++] = (char)cursor->p[-1 + i];
            }
          cursor->p += length - 1;
        }
    }

  return -EINVAL;
}

static int json_parse_number(struct json_cursor *cursor)
{
  const uint8_t *p = cursor->p;

  if (p < cursor->end && *p == '-')
    {
      p++;
    }
  if (p >= cursor->end)
    {
      return -EINVAL;
    }
  if (*p == '0')
    {
      p++;
      if (p < cursor->end && isdigit(*p))
        {
          return -EINVAL;
        }
    }
  else if (*p >= '1' && *p <= '9')
    {
      do
        {
          p++;
        }
      while (p < cursor->end && isdigit(*p));
    }
  else
    {
      return -EINVAL;
    }

  if (p < cursor->end && *p == '.')
    {
      p++;
      if (p >= cursor->end || !isdigit(*p))
        {
          return -EINVAL;
        }
      while (p < cursor->end && isdigit(*p))
        {
          p++;
        }
    }

  if (p < cursor->end && (*p == 'e' || *p == 'E'))
    {
      p++;
      if (p < cursor->end && (*p == '+' || *p == '-'))
        {
          p++;
        }
      if (p >= cursor->end || !isdigit(*p))
        {
          return -EINVAL;
        }
      while (p < cursor->end && isdigit(*p))
        {
          p++;
        }
    }

  cursor->p = p;
  return 0;
}

static int json_parse_value(struct json_cursor *cursor);

static int json_parse_object(struct json_cursor *cursor)
{
  char keys[JSON_OBJECT_FIELDS_MAX][JSON_KEY_MAX];
  int count = 0;
  int ret;

  if (++cursor->depth > JSON_DEPTH_MAX || *cursor->p++ != '{')
    {
      return -EINVAL;
    }

  json_skip_ws(cursor);
  if (cursor->p < cursor->end && *cursor->p == '}')
    {
      cursor->p++;
      cursor->depth--;
      return 0;
    }

  for (;;)
    {
      char key[JSON_KEY_MAX];
      int i;

      if (count >= JSON_OBJECT_FIELDS_MAX)
        {
          return -E2BIG;
        }
      ret = json_parse_string(cursor, key, sizeof(key));
      if (ret != 0)
        {
          return ret;
        }

      for (i = 0; i < count; i++)
        {
          if (strcmp(keys[i], key) == 0)
            {
              return -EEXIST;
            }
        }

      memcpy(keys[count++], key, strlen(key) + 1);
      json_skip_ws(cursor);
      if (cursor->p >= cursor->end || *cursor->p++ != ':')
        {
          return -EINVAL;
        }
      json_skip_ws(cursor);
      ret = json_parse_value(cursor);
      if (ret != 0)
        {
          return ret;
        }
      json_skip_ws(cursor);
      if (cursor->p >= cursor->end)
        {
          return -EINVAL;
        }
      if (*cursor->p == '}')
        {
          cursor->p++;
          cursor->depth--;
          return 0;
        }
      if (*cursor->p++ != ',')
        {
          return -EINVAL;
        }
      json_skip_ws(cursor);
    }
}

static int json_parse_array(struct json_cursor *cursor)
{
  int count = 0;
  int ret;

  if (++cursor->depth > JSON_DEPTH_MAX || *cursor->p++ != '[')
    {
      return -EINVAL;
    }

  json_skip_ws(cursor);
  if (cursor->p < cursor->end && *cursor->p == ']')
    {
      cursor->p++;
      cursor->depth--;
      return 0;
    }

  for (;;)
    {
      if (count++ >= JSON_ARRAY_ITEMS_MAX)
        {
          return -E2BIG;
        }
      ret = json_parse_value(cursor);
      if (ret != 0)
        {
          return ret;
        }
      json_skip_ws(cursor);
      if (cursor->p >= cursor->end)
        {
          return -EINVAL;
        }
      if (*cursor->p == ']')
        {
          cursor->p++;
          cursor->depth--;
          return 0;
        }
      if (*cursor->p++ != ',')
        {
          return -EINVAL;
        }
      json_skip_ws(cursor);
    }
}

static int json_parse_value(struct json_cursor *cursor)
{
  char scratch[JSON_STRING_MAX];

  json_skip_ws(cursor);
  if (cursor->p >= cursor->end)
    {
      return -EINVAL;
    }

  if (*cursor->p == '{')
    {
      return json_parse_object(cursor);
    }
  if (*cursor->p == '[')
    {
      return json_parse_array(cursor);
    }
  if (*cursor->p == '"')
    {
      return json_parse_string(cursor, scratch, sizeof(scratch));
    }
  if (*cursor->p == 't' &&
      (size_t)(cursor->end - cursor->p) >= 4 &&
      !memcmp(cursor->p, "true", 4))
    {
      cursor->p += 4;
      return 0;
    }
  if (*cursor->p == 'f' &&
      (size_t)(cursor->end - cursor->p) >= 5 &&
      !memcmp(cursor->p, "false", 5))
    {
      cursor->p += 5;
      return 0;
    }
  if (*cursor->p == 'n' &&
      (size_t)(cursor->end - cursor->p) >= 4 &&
      !memcmp(cursor->p, "null", 4))
    {
      cursor->p += 4;
      return 0;
    }
  if (*cursor->p == '-' || isdigit(*cursor->p))
    {
      return json_parse_number(cursor);
    }
  return -EINVAL;
}

int vg_config_json_strict_validate(const void *json, size_t length)
{
  struct json_cursor cursor;
  int ret;

  if (json == NULL || length < 2 ||
      length > VG_CONFIG_REQUEST_MAX)
    {
      return -EINVAL;
    }

  cursor.p = json;
  cursor.end = cursor.p + length;
  cursor.depth = 0;
  ret = json_parse_value(&cursor);
  if (ret != 0)
    {
      return ret;
    }

  json_skip_ws(&cursor);
  return cursor.p == cursor.end ? 0 : -EINVAL;
}

static int allowed_keys(const cJSON *object,
                        const char *const *allowed, size_t count)
{
  const cJSON *item;

  if (!cJSON_IsObject(object))
    {
      return -EINVAL;
    }

  cJSON_ArrayForEach(item, object)
    {
      size_t i;
      bool found = false;
      if (item->string == NULL)
        {
          return -EINVAL;
        }
      for (i = 0; i < count; i++)
        {
          if (strcmp(item->string, allowed[i]) == 0)
            {
              found = true;
              break;
            }
        }
      if (!found)
        {
          return -EINVAL;
        }
    }

  return 0;
}

static const cJSON *required_item(const cJSON *object, const char *name,
                                  cJSON_bool type)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (item == NULL)
    {
      return NULL;
    }
  switch (type)
    {
      case cJSON_Object:
        return cJSON_IsObject(item) ? item : NULL;
      case cJSON_Array:
        return cJSON_IsArray(item) ? item : NULL;
      case cJSON_String:
        return cJSON_IsString(item) ? item : NULL;
      case cJSON_Number:
        return cJSON_IsNumber(item) ? item : NULL;
      case cJSON_True:
        return cJSON_IsTrue(item) ? item : NULL;
      default:
        return NULL;
    }
}

static int string_field(const cJSON *object, const char *name,
                        char *out, size_t cap, bool required)
{
  const cJSON *item =
    cJSON_GetObjectItemCaseSensitive(object, name);
  size_t length;

  if (item == NULL)
    {
      return required ? -EINVAL : 0;
    }
  if (!cJSON_IsString(item))
    {
      return -EINVAL;
    }

  length = strlen(item->valuestring);
  if (length >= cap)
    {
      return -E2BIG;
    }

  memcpy(out, item->valuestring, length + 1);
  return 0;
}

static int integer_field(const cJSON *object, const char *name,
                         double minimum, double maximum,
                         bool required, double *out)
{
  const cJSON *item =
    cJSON_GetObjectItemCaseSensitive(object, name);

  if (item == NULL)
    {
      return required ? -EINVAL : -ENOENT;
    }
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
      item->valuedouble < minimum || item->valuedouble > maximum ||
      item->valuedouble != floor(item->valuedouble))
    {
      return -EINVAL;
    }

  *out = item->valuedouble;
  return 0;
}

static bool text_id_max(const char *value, size_t maximum)
{
  size_t i;
  size_t length;

  if (value == NULL)
    {
      return false;
    }
  length = strlen(value);
  if (length < 1 || length > maximum)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      char c = value[i];
      if (!((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-'))
        {
          return false;
        }
    }
  return true;
}

static bool text_id(const char *value)
{
  return text_id_max(value, VG_CONFIG_ID_MAX - 1);
}

static bool text_without_controls(const char *value, size_t maximum,
                                  bool allow_lf)
{
  const uint8_t *cursor;
  const uint8_t *end;
  size_t length;

  if (value == NULL)
    {
      return false;
    }

  length = strlen(value);
  if (length < 1 || length > maximum)
    {
      return false;
    }

  cursor = (const uint8_t *)value;
  end = cursor + length;
  while (cursor < end)
    {
      uint32_t codepoint;
      size_t used;

      if (utf8_codepoint(cursor, (size_t)(end - cursor),
                         &codepoint, &used) != 0)
        {
          return false;
        }
      if ((codepoint < 0x20 && (!allow_lf || codepoint != '\n')) ||
          (codepoint >= 0x7f && codepoint <= 0x9f))
        {
          return false;
        }
      cursor += used;
    }

  return true;
}

static bool quote_text(const char *value)
{
  return text_without_controls(value, VG_CONFIG_QUOTE_MAX - 1, true);
}

static bool lowercase_hex(const char *value, size_t length)
{
  size_t i;
  if (value == NULL || strlen(value) != length)
    {
      return false;
    }
  for (i = 0; i < length; i++)
    {
      if (!((value[i] >= '0' && value[i] <= '9') ||
            (value[i] >= 'a' && value[i] <= 'f')))
        {
          return false;
        }
    }
  return true;
}

static int parse_evidence(const cJSON *array,
                          struct vg_config_candidate *candidate)
{
  const cJSON *item;
  int count = cJSON_GetArraySize(array);

  if (count < 1 || count > VG_CONFIG_EVIDENCE_MAX)
    {
      return -E2BIG;
    }

  cJSON_ArrayForEach(item, array)
    {
      static const char *const keys[] =
      {
        "id", "page", "quote"
      };
      struct vg_config_evidence *evidence;
      double page;
      uint32_t i;

      if (allowed_keys(item, keys, 3) != 0)
        {
          return -EINVAL;
        }
      evidence = &candidate->evidence[candidate->evidence_count];
      if (string_field(item, "id", evidence->id, sizeof(evidence->id),
                       true) != 0 ||
          !text_id_max(evidence->id, VG_CONFIG_EVIDENCE_ID_MAX - 1) ||
          integer_field(item, "page", 1, UINT32_MAX, true, &page) != 0 ||
          string_field(item, "quote", evidence->quote,
                       sizeof(evidence->quote), true) != 0)
        {
          return -EINVAL;
        }
      evidence->page = (uint32_t)page;
      if (!quote_text(evidence->quote))
        {
          return -EINVAL;
        }
      for (i = 0; i < candidate->evidence_count; i++)
        {
          if (strcmp(candidate->evidence[i].id, evidence->id) == 0)
            {
              return -EEXIST;
            }
        }
      candidate->evidence_count++;
    }
  return 0;
}

static int parse_unsigned_address(const char *text, uint32_t *out)
{
  uint32_t value = 0;
  uint32_t base = 10;
  size_t i = 0;
  size_t length;

  if (text == NULL)
    {
      return -EINVAL;
    }
  length = strlen(text);
  if (length < 1 || length > 32)
    {
      return -EINVAL;
    }
  if (length > 2 && text[0] == '0' &&
      text[1] == 'x')
    {
      base = 16;
      i = 2;
    }
  if (i >= length)
    {
      return -EINVAL;
    }

  for (; i < length; i++)
    {
      int digit = hex_value((unsigned char)text[i]);
      if ((base == 10 && (digit < 0 || digit > 9)) ||
          (base == 16 && digit < 0) ||
          value > (UINT32_MAX - (uint32_t)digit) / base)
        {
          return -EINVAL;
        }
      value = value * base + (uint32_t)digit;
    }

  if (value > 105536)
    {
      return -EINVAL;
    }

  *out = value;
  return 0;
}

static int evidence_index(struct vg_config_candidate *candidate,
                          const char *id)
{
  uint32_t i;
  for (i = 0; i < candidate->evidence_count; i++)
    {
      if (strcmp(candidate->evidence[i].id, id) == 0)
        {
          return (int)i;
        }
    }
  return -1;
}

static int parse_source(const cJSON *source,
                        struct vg_config_candidate *candidate,
                        struct vg_config_candidate_point *point)
{
  static const char *const keys[] =
  {
    "evidence_ids", "original_address", "address_base"
  };
  const cJSON *ids;
  const cJSON *id;
  uint32_t original;
  double base_value;
  uint32_t base;
  uint32_t reg;
  int count;

  if (allowed_keys(source, keys, 3) != 0 ||
      string_field(source, "original_address", point->original_address,
                   sizeof(point->original_address), true) != 0 ||
      parse_unsigned_address(point->original_address, &original) != 0 ||
      integer_field(source, "address_base", 0, 65535, true,
                    &base_value) != 0)
    {
      return -EINVAL;
    }

  base = (uint32_t)base_value;
  if (base != 0 && base != 1 && base != 30001 && base != 40001)
    {
      return -EINVAL;
    }
  if (base == 30001 && point->fc != 4)
    {
      return -EINVAL;
    }
  if (base == 40001 && point->fc != 3)
    {
      return -EINVAL;
    }
  if (original < base || original - base > UINT16_MAX)
    {
      return -EINVAL;
    }
  reg = original - base;
  if (point->reg != reg)
    {
      return -EINVAL;
    }
  point->address_base = base;

  ids = required_item(source, "evidence_ids", cJSON_Array);
  if (ids == NULL)
    {
      return -EINVAL;
    }
  count = cJSON_GetArraySize(ids);
  if (count < 1 || count > VG_CONFIG_EVIDENCE_REF_MAX)
    {
      return -EINVAL;
    }
  cJSON_ArrayForEach(id, ids)
    {
      uint8_t i;
      if (!cJSON_IsString(id) ||
          !text_id_max(id->valuestring, VG_CONFIG_EVIDENCE_ID_MAX - 1) ||
          evidence_index(candidate, id->valuestring) < 0)
        {
          return -EINVAL;
        }
      for (i = 0; i < point->evidence_count; i++)
        {
          if (strcmp(point->evidence_ids[i], id->valuestring) == 0)
            {
              return -EINVAL;
            }
        }
      memcpy(point->evidence_ids[point->evidence_count++],
             id->valuestring, strlen(id->valuestring) + 1);
    }
  return 0;
}

static int parse_points(const cJSON *array,
                        struct vg_config_candidate *candidate)
{
  static const char *const point_keys[] =
  {
    "id", "name", "addr", "fc", "reg", "qty", "dtype", "scale",
    "offset", "unit", "source"
  };
  const cJSON *item;
  int count = cJSON_GetArraySize(array);

  if (count < 1 || count > VG_CONFIG_CANDIDATE_POINT_MAX)
    {
      return -E2BIG;
    }

  cJSON_ArrayForEach(item, array)
    {
      struct vg_config_candidate_point *point =
        &candidate->points[candidate->point_count];
      const cJSON *source;
      const cJSON *scale_item;
      double value;
      float scale;
      uint32_t i;
      size_t unit_length;

      if (allowed_keys(item, point_keys, 11) != 0 ||
          string_field(item, "id", point->id, sizeof(point->id), true) != 0 ||
          !vg_point_validate_id(point->id) ||
          string_field(item, "name", point->name,
                       sizeof(point->name), true) != 0 ||
          !vg_point_validate_name(point->name) ||
          !text_without_controls(point->name,
                                 sizeof(point->name) - 1, false) ||
          integer_field(item, "addr", 1, 247, true, &value) != 0)
        {
          return -EINVAL;
        }
      point->addr = (uint8_t)value;
      if (point->addr != candidate->slave_address ||
          integer_field(item, "fc", 3, 4, true, &value) != 0)
        {
          return -EINVAL;
        }
      point->fc = (uint8_t)value;
      if ((point->fc != 3 && point->fc != 4) ||
          integer_field(item, "reg", 0, UINT16_MAX, true, &value) != 0)
        {
          return -EINVAL;
        }
      point->reg = (uint16_t)value;
      if (integer_field(item, "qty", 1, 1, true, &value) != 0 ||
          string_field(item, "dtype", point->dtype,
                       sizeof(point->dtype), true) != 0 ||
          (strcmp(point->dtype, "int16") != 0 &&
           strcmp(point->dtype, "uint16") != 0))
        {
          return -EINVAL;
        }
      scale_item =
        cJSON_GetObjectItemCaseSensitive(item, "scale");
      if (!cJSON_IsNumber(scale_item) ||
          !isfinite(scale_item->valuedouble))
        {
          return -EINVAL;
        }
      scale = (float)scale_item->valuedouble;
      if (!isfinite(scale) || scale == 0.0f ||
          integer_field(item, "offset", 0, 0, true, &value) != 0 ||
          string_field(item, "unit", point->unit,
                       sizeof(point->unit), true) != 0)
        {
          return -EINVAL;
        }
      point->scale = scale;
      unit_length = strlen(point->unit);
      if (unit_length > 7)
        {
          return -EINVAL;
        }
      if (unit_length > 0 &&
          !text_without_controls(point->unit, 7, false))
        {
          return -EINVAL;
        }
      if (unit_length > 0 && point->unit[0] <= 0x20)
        {
          return -EINVAL;
        }
      for (i = 0; i < unit_length; i++)
        {
          unsigned char c = (unsigned char)point->unit[i];
          if (c == 0x20)
            {
              return -EINVAL;
            }
        }
      source = required_item(item, "source", cJSON_Object);
      if (source == NULL ||
          parse_source(source, candidate, point) != 0)
        {
          return -EINVAL;
        }

      for (i = 0; i < candidate->point_count; i++)
        {
          struct vg_config_candidate_point *prior =
            &candidate->points[i];
          if (strcmp(prior->id, point->id) == 0 ||
              (prior->fc == point->fc && prior->reg == point->reg))
            {
              return -EEXIST;
            }
        }
      candidate->point_count++;
    }
  return 0;
}

static int is_empty_table(const struct vg_discover_summary *table);

static int merge_candidate(const struct vg_config_candidate *candidate,
                           const struct vg_discover_summary *current,
                           struct vg_discover_summary *merged)
{
  bool referenced[VG_CONFIG_EVIDENCE_MAX];
  uint32_t i;
  uint32_t j;

  memset(referenced, 0, sizeof(referenced));
  *merged = *current;
  if (!is_empty_table(current))
    {
      for (i = 0; i < candidate->point_count; i++)
        {
          for (j = 0; j < (uint32_t)current->n_points; j++)
            {
              if (current->points[j].addr == candidate->points[i].addr ||
                  strcmp(current->points[j].id,
                         candidate->points[i].id) == 0)
                {
                  return -EEXIST;
                }
            }
        }
    }

  if ((uint32_t)current->n_points + candidate->point_count >
      VG_DISCOVER_MAX_POINTS)
    {
      return -E2BIG;
    }

  for (i = 0; i < candidate->point_count; i++)
    {
      const struct vg_config_candidate_point *source =
        &candidate->points[i];
      struct vg_point_entry *point;

      for (j = 0; j < source->evidence_count; j++)
        {
          int index = -1;
          uint32_t k;
          for (k = 0; k < candidate->evidence_count; k++)
            {
              if (strcmp(candidate->evidence[k].id,
                         source->evidence_ids[j]) == 0)
                {
                  index = (int)k;
                  break;
                }
            }
          if (index < 0)
            {
              return -EINVAL;
            }
          referenced[index] = true;
        }

      point = &merged->points[merged->n_points++];
      memset(point, 0, sizeof(*point));
      memcpy(point->id, source->id, sizeof(point->id));
      memcpy(point->name, source->name, sizeof(point->name));
      point->addr = source->addr;
      point->fc = source->fc;
      point->reg = source->reg;
      point->qty = 1;
      memcpy(point->dtype, source->dtype, sizeof(point->dtype));
      point->scale = source->scale;
      memcpy(point->unit, source->unit, sizeof(point->unit));
      point->fail_n = 3;
    }

  for (i = 0; i < candidate->evidence_count; i++)
    {
      if (!referenced[i])
        {
          return -EINVAL;
        }
    }

  return 0;
}

static int is_empty_table(const struct vg_discover_summary *table)
{
  return table == NULL || table->n_points == 0;
}

static void hash_to_hex(const uint8_t digest[32], char out[65])
{
  static const char digits[] = "0123456789abcdef";
  size_t i;
  for (i = 0; i < 32; i++)
    {
      out[i * 2] = digits[digest[i] >> 4];
      out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
  out[64] = '\0';
}

int vg_config_candidate_parse_json(
  const void *json,
  size_t length,
  struct vg_config_candidate *out)
{
  static const char *const root_keys[] =
  {
    "candidate_schema_version", "mode", "bus", "sensor",
    "manual", "evidence", "points"
  };
  static const char *const bus_keys[] = { "baud" };
  static const char *const sensor_keys[] =
  {
    "slave_address", "model"
  };
  static const char *const manual_keys[] =
  {
    "manual_id", "sha256"
  };
  cJSON *doc = NULL;
  const cJSON *root;
  const cJSON *bus;
  const cJSON *sensor;
  const cJSON *manual;
  const cJSON *evidence;
  const cJSON *points;
  double value;
  int ret;

  if (json == NULL || out == NULL)
    {
      return -EINVAL;
    }

  ret = vg_config_json_strict_validate(json, length);
  if (ret != 0)
    {
      return ret;
    }

  memset(out, 0, sizeof(*out));
  {
    char *copy = malloc(length + 1);
    if (copy == NULL)
      {
        return -ENOMEM;
      }
    memcpy(copy, json, length);
    copy[length] = '\0';
    doc = cJSON_ParseWithOpts(copy, NULL, 1);
    memset(copy, 0, length);
    free(copy);
  }
  if (doc == NULL)
    {
      return -EINVAL;
    }

  root = doc;
  if (allowed_keys(root, root_keys, 7) != 0 ||
      integer_field(root, "candidate_schema_version", 1, 1, true,
                    &value) != 0)
    {
      ret = -EINVAL;
      goto done;
    }
  out->schema_version = (uint32_t)value;
  ret = string_field(root, "mode", out->mode, sizeof(out->mode), true);
  if (ret != 0 || strcmp(out->mode, "add_sensor") != 0)
    {
      ret = -EINVAL;
      goto done;
    }

  bus = required_item(root, "bus", cJSON_Object);
  if (bus == NULL || allowed_keys(bus, bus_keys, 1) != 0 ||
      integer_field(bus, "baud", 9600, 9600, true, &value) != 0)
    {
      ret = -EINVAL;
      goto done;
    }
  out->baud = 9600;

  sensor = required_item(root, "sensor", cJSON_Object);
  if (sensor == NULL || allowed_keys(sensor, sensor_keys, 2) != 0 ||
      integer_field(sensor, "slave_address", 1, 247, true, &value) != 0 ||
      string_field(sensor, "model", out->model,
                   sizeof(out->model), true) != 0 ||
      !text_without_controls(out->model, sizeof(out->model) - 1, false))
    {
      ret = -EINVAL;
      goto done;
    }
  out->slave_address = (uint8_t)value;

  manual = required_item(root, "manual", cJSON_Object);
  if (manual == NULL || allowed_keys(manual, manual_keys, 2) != 0 ||
      string_field(manual, "manual_id", out->manual_id,
                   sizeof(out->manual_id), true) != 0 ||
      !text_id(out->manual_id) ||
      string_field(manual, "sha256", out->manual_sha256,
                   sizeof(out->manual_sha256), true) != 0 ||
      !lowercase_hex(out->manual_sha256, 64))
    {
      ret = -EINVAL;
      goto done;
    }

  evidence = required_item(root, "evidence", cJSON_Array);
  points = required_item(root, "points", cJSON_Array);
  if (evidence == NULL || points == NULL)
    {
      ret = -EINVAL;
      goto done;
    }

  ret = parse_evidence(evidence, out);
  if (ret == 0)
    {
      ret = parse_points(points, out);
    }
  if (ret != 0)
    {
      goto done;
    }

  ret = 0;

 done:
  cJSON_Delete(doc);
  if (ret != 0)
    {
      memset(out, 0, sizeof(*out));
    }
  return ret;
}

int vg_config_candidate_build_json(
  const void *json, size_t length,
  const struct vg_discover_summary *current,
  struct vg_config_candidate_build *out)
{
  struct vg_ota_sha256 sha;
  uint8_t digest[32];
  int ret;

  if (current == NULL || out == NULL || current->n_points < 0 ||
      current->n_points > VG_DISCOVER_MAX_POINTS)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));
  ret = vg_config_candidate_parse_json(json, length, &out->candidate);
  if (ret == 0)
    {
      ret = merge_candidate(&out->candidate, current, &out->merged);
    }
  if (ret == 0)
    {
      ret = vg_point_table_serialize(&out->merged, out->target_json,
                                     sizeof(out->target_json),
                                     &out->target_len);
    }
  if (ret != 0)
    {
      memset(out, 0, sizeof(*out));
      return ret;
    }

  vg_ota_sha256_init(&sha);
  vg_ota_sha256_update(&sha, (const uint8_t *)out->target_json,
                       (uint32_t)out->target_len);
  vg_ota_sha256_final(&sha, digest);
  hash_to_hex(digest, out->candidate_hash);
  memset(&sha, 0, sizeof(sha));
  memset(digest, 0, sizeof(digest));
  return 0;
}
