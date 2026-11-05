/****************************************************************************
 * app/velaguard/vg_config_mqtt_runtime.c
 ****************************************************************************/

#ifdef __NuttX__
#  include <nuttx/config.h>
#endif

#include "vg_config_mqtt_runtime.h"

#include "vg_config_confirmation_worker.h"
#include "vg_config_file_source.h"
#include "vg_config_mqtt_credentials.h"
#include "vg_config_mqtt_service.h"
#include "vg_config_mqtt_transport.h"
#include "vg_device_id.h"
#include "vg_discover.h"
#include "vg_ota_https.h"
#include "vg_ota_trust.h"

#ifdef __NuttX__
#  include "vg_net_mgr.h"
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <time.h>
#  include <unistd.h>
#endif

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_VG_DISCOVER_POINTS_PATH
#  define CONFIG_VG_DISCOVER_POINTS_PATH \
    "/data/velaguard/config/points.json"
#endif

#ifndef CONFIG_VG_STORE_ROOT
#  define CONFIG_VG_STORE_ROOT "/data"
#endif

struct runtime
{
  struct vg_config_mqtt_config credentials;
  struct vg_config_file_source table_source;
  struct vg_config_service config;
  struct vg_config_mqtt_transport transport;
  struct vg_config_mqtt_service service;
  struct vg_config_confirmation_worker worker;
  bool worker_initialized;
  bool started;
};

static pthread_mutex_t g_start_lock = PTHREAD_MUTEX_INITIALIZER;
static struct runtime *g_runtime_ptr;

#define g_runtime (*g_runtime_ptr)
#define VG_CONFIG_MQTT_RUNTIME_HEAP_RESERVE (64u * 1024u)

static const struct runtime g_runtime_initializer;

static int load_table(void *ctx, struct vg_config_table_snapshot *out)
{
  return vg_config_file_source_load(ctx, out);
}

static int random_bytes(void *ctx, uint8_t *out, size_t length)
{
  size_t used = 0;
  int fd;

  (void)ctx;
  if (out == NULL)
    {
      return -EINVAL;
    }
#ifdef __NuttX__
  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  while (used < length)
    {
      ssize_t got = read(fd, out + used, length - used);
      if (got < 0 && errno == EINTR)
        {
          continue;
        }
      if (got <= 0)
        {
          close(fd);
          memset(out, 0, length);
          return -EIO;
        }
      used += (size_t)got;
    }
  if (close(fd) != 0)
    {
      memset(out, 0, length);
      return -errno;
    }
  return 0;
#else
  while (used < length)
    {
      out[used++] = (uint8_t)(0xa5u ^ used);
    }
  return 0;
#endif
}

static uint64_t monotonic_ms(void *ctx)
{
  (void)ctx;
#ifdef __NuttX__
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    {
      return 0;
    }
  return (uint64_t)value.tv_sec * 1000u +
         (uint64_t)value.tv_nsec / 1000000u;
#else
  return 0;
#endif
}

static int commit_table(void *ctx, const void *target_json,
                        size_t target_len,
                        const struct vg_discover_summary *target_table)
{
  (void)ctx;
  if (target_table == NULL)
    {
      return -EINVAL;
    }
  return vg_point_table_commit_json(
    target_json, target_len, CONFIG_VG_DISCOVER_POINTS_PATH);
}

static int notify_table(void *ctx)
{
  (void)ctx;
  return 0;
}

static int wire_open(void *ctx, const char *device_id)
{
  struct runtime *runtime = ctx;

  return vg_config_mqtt_transport_open(
    &runtime->transport, &runtime->credentials, device_id,
    vg_ota_trust_ca());
}

static int wire_poll(void *ctx)
{
  struct runtime *runtime = ctx;
  return vg_config_mqtt_transport_poll(&runtime->transport);
}

static bool wire_online(void *ctx)
{
  struct runtime *runtime = ctx;
  return vg_config_mqtt_transport_online(&runtime->transport);
}

static void wire_close(void *ctx)
{
  struct runtime *runtime = ctx;
  vg_config_mqtt_transport_close(&runtime->transport);
}

static struct vg_config_mqtt_message *wire_peek(void *ctx)
{
  struct runtime *runtime = ctx;
  return (struct vg_config_mqtt_message *)
    vg_config_mqtt_transport_peek(&runtime->transport);
}

static void wire_release(void *ctx)
{
  struct runtime *runtime = ctx;
  vg_config_mqtt_transport_release(&runtime->transport);
}

static int wire_publish(void *ctx, const char *json, uint16_t *ticket)
{
  struct runtime *runtime = ctx;
  return vg_config_mqtt_transport_publish_state(
    &runtime->transport, json, ticket);
}

static int wire_acked(void *ctx, uint16_t ticket)
{
  struct runtime *runtime = ctx;
  return vg_config_mqtt_transport_acked(&runtime->transport, ticket);
}

static bool wire_ready(void *ctx)
{
#ifdef __NuttX__
  struct vg_net_live_status status;
  (void)ctx;
  if (vg_net_mgr_status(&status) != 0)
    {
      return false;
    }
  if (status.egress == VG_EGRESS_RJ45)
    {
      return status.rj45_has_ip;
    }
  if (status.egress == VG_EGRESS_WIFI)
    {
      return status.wifi_has_ip;
    }
  return false;
#else
  (void)ctx;
  return true;
#endif
}

static uint64_t wire_now(void *ctx)
{
  return monotonic_ms(ctx);
}

static void wire_delay(void *ctx, uint32_t delay_ms)
{
  (void)ctx;
#ifdef __NuttX__
  usleep(delay_ms * 1000u);
#else
  (void)delay_ms;
#endif
}

static int start_locked(void)
{
  struct vg_config_service_ports config_ports;
  struct vg_config_mqtt_service_ports wire_ports;
  int ret;

  if (g_runtime_ptr != NULL && g_runtime.started)
    {
      return 0;
    }

  if (!vg_ota_tls_entropy_ready())
    {
      return -EAGAIN;
    }
  if (vg_ota_trust_ca() == NULL || vg_ota_trust_ca()[0] == '\0')
    {
      return -ENOKEY;
    }
  g_runtime_ptr = malloc(sizeof(*g_runtime_ptr));
  if (g_runtime_ptr == NULL)
    {
      return -ENOMEM;
    }
  *g_runtime_ptr = g_runtime_initializer;
#ifdef __NuttX__
  if (mallinfo().fordblks < VG_CONFIG_MQTT_RUNTIME_HEAP_RESERVE)
    {
      ret = -ENOMEM;
      goto failed_runtime;
    }
#endif
  ret = vg_config_mqtt_credentials_load(&g_runtime.credentials);
  if (ret != 0)
    {
      return ret;
    }

  ret = vg_config_file_source_init(
    &g_runtime.table_source, CONFIG_VG_DISCOVER_POINTS_PATH,
    CONFIG_VG_STORE_ROOT);
  if (ret != 0)
    {
      goto failed_credentials;
    }

  memset(&config_ports, 0, sizeof(config_ports));
  config_ports.load_table = load_table;
  config_ports.random_bytes = random_bytes;
  config_ports.now_ms = monotonic_ms;
  config_ports.ctx = &g_runtime.table_source;
  config_ports.commit_table = commit_table;
  config_ports.notify_table = notify_table;
  ret = vg_config_service_init(&g_runtime.config, &config_ports);
  if (ret != 0)
    {
      goto failed_credentials;
    }

  memset(&wire_ports, 0, sizeof(wire_ports));
  wire_ports.open_connection = wire_open;
  wire_ports.poll_connection = wire_poll;
  wire_ports.online = wire_online;
  wire_ports.close_connection = wire_close;
  wire_ports.peek = wire_peek;
  wire_ports.release = wire_release;
  wire_ports.publish = wire_publish;
  wire_ports.acked = wire_acked;
  wire_ports.ready = wire_ready;
  wire_ports.now_ms = wire_now;
  wire_ports.delay_ms = wire_delay;
  wire_ports.ctx = &g_runtime;
  ret = vg_config_mqtt_service_init(
    &g_runtime.service, &g_runtime.config, vg_device_id(), &wire_ports);
  if (ret != 0)
    {
      goto failed_credentials;
    }
  ret = vg_config_mqtt_service_start(&g_runtime.service);
  if (ret != 0)
    {
      goto failed_credentials;
    }

  g_runtime.started = true;
  return 0;

failed_credentials:
  memset(&g_runtime.credentials, 0, sizeof(g_runtime.credentials));
failed_runtime:
  memset(g_runtime_ptr, 0, sizeof(*g_runtime_ptr));
  free(g_runtime_ptr);
  g_runtime_ptr = NULL;
  return ret;
}

int vg_config_mqtt_runtime_start(void)
{
  int ret;

  pthread_mutex_lock(&g_start_lock);
  ret = start_locked();
  pthread_mutex_unlock(&g_start_lock);
  return ret;
}

int vg_config_mqtt_runtime_status(
  struct vg_config_mqtt_runtime_status *out)
{
  struct vg_config_mqtt_service_status status;
  int ret = 0;

  if (out == NULL)
    {
      return -EINVAL;
    }
  memset(out, 0, sizeof(*out));
  pthread_mutex_lock(&g_start_lock);
  out->initialized = g_runtime_ptr != NULL && g_runtime.started;
  if (!out->initialized)
    {
#ifdef __NuttX__
      struct stat st;
#endif
      snprintf(out->error, sizeof(out->error), "%s",
               vg_ota_trust_ca() == NULL || vg_ota_trust_ca()[0] == '\0' ?
               "trust_unconfigured" :
#ifdef __NuttX__
               stat(VG_CONFIG_MQTT_CREDENTIALS_FILE, &st) == 0 ?
               "restart_required" :
#endif
               "credentials_unconfigured");
      goto done;
    }
  if (vg_config_mqtt_service_status(&g_runtime.service, &status) != 0)
    {
      ret = -EIO;
      goto done;
    }
  out->running = status.running;
  out->online = status.online;
  memcpy(out->error, status.error, sizeof(out->error));

done:
  pthread_mutex_unlock(&g_start_lock);
  return ret;
}

void vg_config_mqtt_runtime_print(FILE *out)
{
  struct vg_config_mqtt_runtime_status status;

  if (out == NULL)
    {
      return;
    }
  (void)vg_config_mqtt_runtime_status(&status);
  fprintf(out,
          "vgconfig-mqtt: initialized=%d running=%d online=%d error=%s\n",
          status.initialized, status.running, status.online,
          status.error[0] ? status.error : "none");
}

struct vg_config_service *vg_config_mqtt_runtime_service(void)
{
  struct vg_config_service *service = NULL;

  pthread_mutex_lock(&g_start_lock);
  if (g_runtime_ptr != NULL && g_runtime.started)
    {
      service = &g_runtime.config;
    }
  pthread_mutex_unlock(&g_start_lock);
  return service;
}

int vg_config_mqtt_runtime_bind_confirmation(
  struct vg_config_confirmation *confirmation,
  bool (*alarm_active)(void *ctx),
  void *alarm_ctx)
{
  struct vg_config_confirmation_ports ports;
  int ret;

  if (confirmation == NULL)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_start_lock);
  if (g_runtime_ptr == NULL || !g_runtime.started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return -EAGAIN;
    }
  if (g_runtime.worker_initialized)
    {
      pthread_mutex_unlock(&g_start_lock);
      return -EALREADY;
    }

  ret = vg_config_confirmation_worker_init(
    &g_runtime.worker, confirmation, &g_runtime.config,
    alarm_active, alarm_ctx);
  if (ret != 0)
    {
      pthread_mutex_unlock(&g_start_lock);
      return ret;
    }
  ret = vg_config_confirmation_worker_ports(&g_runtime.worker, &ports);
  if (ret == 0)
    {
      ret = vg_config_confirmation_bind(confirmation, &ports);
    }
  if (ret == 0)
    {
      ret = vg_config_confirmation_worker_start(&g_runtime.worker);
    }
  if (ret != 0)
    {
      pthread_mutex_unlock(&g_start_lock);
      return ret;
    }

  g_runtime.worker_initialized = true;
  pthread_mutex_unlock(&g_start_lock);
  return 0;
}
