#include "vg_config_mqtt_protocol.h"
#include "vg_config_mqtt_transport.h"
#include "vg_ota_trust_build.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void)
{
  struct timespec value;

  clock_gettime(CLOCK_MONOTONIC, &value);
  return (uint64_t)value.tv_sec * 1000u +
         (uint64_t)value.tv_nsec / 1000000u;
}

int vg_mqtt_pal_try_sendall(int fd, const void *bytes, size_t length,
                            int flags, ssize_t *out)
{
  (void)flags;
  return vg_config_mqtt_pal_send(fd, bytes, length, out);
}

int vg_mqtt_pal_try_recvall(int fd, void *bytes, size_t length,
                            int flags, ssize_t *out)
{
  (void)flags;
  return vg_config_mqtt_pal_recv(fd, bytes, length, out);
}

int main(int argc, char **argv)
{
  struct vg_config_mqtt_transport transport;
  struct vg_config_mqtt_config config;
  struct vg_config_mqtt_message message;
  struct vg_config_mqtt_request request;
  char candidate_buffer[VG_CONFIG_MQTT_REQUEST_MAX + 1];
  uint16_t ticket = 0;
  uint64_t deadline;
  char error[64];
  const char *mode;
  int accepted = 0;
  int rejected = 0;
  int ret;

  assert(argc == 3);
  memset(&transport, 0, sizeof(transport));
  memset(&config, 0, sizeof(config));
  strcpy(config.broker, "localhost");
  snprintf(config.port, sizeof(config.port), "%s", argv[1]);
  strcpy(config.username, "config-unit");
  strcpy(config.password, "config-synthetic-password");
  mode = argv[2];
  if (strcmp(mode, "wrong-auth") == 0)
    {
      strcpy(config.password, "wrong-synthetic-password");
    }
  if (strcmp(mode, "wrong-host") == 0)
    {
      strcpy(config.broker, "127.0.0.1");
    }

  ret = vg_config_mqtt_transport_open(
    &transport, &config, "vg-test", VG_OTA_CA_PEM);
  if (strcmp(mode, "wrong-auth") == 0 ||
      strcmp(mode, "wrong-host") == 0 ||
      strcmp(mode, "deny-sub") == 0)
    {
      assert(ret < 0);
      assert(!vg_config_mqtt_transport_online(&transport));
      puts("config mqtt helper: rejected as expected");
      memset(&config, 0, sizeof(config));
      return 0;
    }
  if (ret != 0)
    {
      fprintf(stderr, "config mqtt helper: open failed %d\n", ret);
    }
  assert(ret == 0);
  assert(vg_config_mqtt_transport_online(&transport));

  if (strcmp(mode, "oversize") == 0)
    {
      deadline = now_ms() + 5000;
      ret = 0;
      while (vg_config_mqtt_transport_online(&transport) &&
             now_ms() < deadline)
        {
          ret = vg_config_mqtt_transport_poll(&transport);
          if (ret != 0)
            {
              break;
            }
          usleep(10000);
        }
      assert(ret != 0);
      assert(!vg_config_mqtt_transport_online(&transport));
      memset(&config, 0, sizeof(config));
      memset(&transport, 0, sizeof(transport));
      puts("config mqtt helper: oversized request disconnected");
      return 0;
    }

  deadline = now_ms() + 5000;
  while (accepted + rejected < 4 && now_ms() < deadline)
    {
      ret = vg_config_mqtt_transport_poll(&transport);
      if (ret != 0)
        {
          fprintf(stderr, "config mqtt helper: poll failed %d\n", ret);
        }
      assert(ret == 0);
      if (vg_config_mqtt_transport_take(&transport, &message) != 0)
        {
          usleep(10000);
          continue;
        }
      if (vg_config_mqtt_request_parse(
            message.topic, message.payload, message.length,
            "vg-test", true, message.retain, message.qos,
            &request, candidate_buffer, sizeof(candidate_buffer),
            error, sizeof(error)) == 0)
        {
          accepted++;
          assert(request.op == VG_CONFIG_MQTT_QUERY_DEVICE);
          {
            struct vg_config_device_snapshot snapshot;
            char json[VG_CONFIG_STATE_MAX];

            memset(&snapshot, 0, sizeof(snapshot));
            memset(snapshot.boot_id, '1', sizeof(snapshot.boot_id) - 1);
            memset(snapshot.link_id, '2', sizeof(snapshot.link_id) - 1);
            snapshot.next_open_seq = 1;
            snapshot.config_state = VG_CONFIG_TABLE_EMPTY;
            snapshot.remote_config = true;
            snapshot.max_points = VG_DISCOVER_MAX_POINTS;
            snapshot.max_request_bytes = VG_CONFIG_REQUEST_MAX;
            snapshot.max_state_bytes = VG_CONFIG_STATE_MAX;
            snapshot.lease_ms = VG_CONFIG_LEASE_MS;
            snapshot.baud = 9600;
            assert(vg_config_mqtt_device_snapshot_json(
                     json, sizeof(json), "vg-test", request.req_id,
                     &snapshot) == 0);
            assert(vg_config_mqtt_transport_publish_state(
                     &transport, json, &ticket) == 0);
          }
        }
      else
        {
          rejected++;
        }
      memset(&message, 0, sizeof(message));
    }
  assert(accepted == 1 && rejected == 3);

  deadline = now_ms() + 3000;
  while (vg_config_mqtt_transport_acked(&transport, ticket) == 0 &&
         now_ms() < deadline)
    {
      assert(vg_config_mqtt_transport_poll(&transport) == 0);
      usleep(10000);
    }
  assert(vg_config_mqtt_transport_acked(&transport, ticket) == 1);
  vg_config_mqtt_transport_close(&transport);

  memset(&config, 0, sizeof(config));
  memset(&transport, 0, sizeof(transport));
  puts("config mqtt helper: OK (TLS, auth, SUBACK, QoS1, retained and device rejection)");
  return 0;
}
