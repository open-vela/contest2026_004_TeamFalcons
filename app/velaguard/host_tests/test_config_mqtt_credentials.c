#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../vg_config_mqtt_credentials.h"

int main(void)
{
  const char *valid =
    "{\"broker\":\"broker.example\",\"port\":\"8883\","
    "\"username\":\"device-user\",\"password\":\"synthetic-secret\"}";
  const char *duplicate =
    "{\"broker\":\"broker.example\",\"broker\":\"other.example\","
    "\"port\":\"8883\",\"username\":\"device-user\","
    "\"password\":\"synthetic-secret\"}";
  const char *unknown =
    "{\"broker\":\"broker.example\",\"port\":\"8883\","
    "\"username\":\"device-user\",\"password\":\"synthetic-secret\","
    "\"ca\":\"not-in-json\"}";
  const char *bad_port =
    "{\"broker\":\"broker.example\",\"port\":\"70000\","
    "\"username\":\"device-user\",\"password\":\"synthetic-secret\"}";
  const char *control =
    "{\"broker\":\"broker.example\",\"port\":\"8883\","
    "\"username\":\"device\\u0001user\","
    "\"password\":\"synthetic-secret\"}";
  struct vg_config_mqtt_config out;

  assert(vg_config_mqtt_credentials_parse(
           valid, strlen(valid), &out) == 0);
  assert(strcmp(out.broker, "broker.example") == 0);
  assert(strcmp(out.port, "8883") == 0);
  assert(strcmp(out.username, "device-user") == 0);
  assert(strcmp(out.password, "synthetic-secret") == 0);
  memset(&out, 0, sizeof(out));

  assert(vg_config_mqtt_credentials_parse(
           duplicate, strlen(duplicate), &out) == -EINVAL);
  assert(vg_config_mqtt_credentials_parse(
           unknown, strlen(unknown), &out) == -EINVAL);
  assert(vg_config_mqtt_credentials_parse(
           bad_port, strlen(bad_port), &out) == -EINVAL);
  assert(vg_config_mqtt_credentials_parse(
           control, strlen(control), &out) == -EINVAL);
  assert(out.password[0] == '\0');
  puts("test_config_mqtt_credentials: PASS");
  return 0;
}
