/****************************************************************************
 * app/velaguard/vg_config_mqtt_service.c
 ****************************************************************************/

#ifdef __NuttX__
#  include <nuttx/config.h>
#endif

#include "vg_config_mqtt_service.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define VG_CONFIG_MQTT_ACK_TIMEOUT_MS 15000u
#define VG_CONFIG_MQTT_SERVICE_STACK 32768u

static void set_error(struct vg_config_mqtt_service *service,
                      const char *error)
{
  pthread_mutex_lock(&service->lock);
  snprintf(service->error, sizeof(service->error), "%s",
           error != NULL ? error : "");
  pthread_mutex_unlock(&service->lock);
}

static void clear_error(struct vg_config_mqtt_service *service)
{
  set_error(service, "");
}

static bool ports_valid(const struct vg_config_mqtt_service_ports *ports)
{
  return ports != NULL && ports->open_connection != NULL &&
         ports->poll_connection != NULL && ports->online != NULL &&
         ports->close_connection != NULL && ports->peek != NULL &&
         ports->release != NULL && ports->publish != NULL &&
         ports->acked != NULL && ports->ready != NULL &&
         ports->now_ms != NULL && ports->delay_ms != NULL;
}

static void link_lost(struct vg_config_mqtt_service *service,
                      const char *reason)
{
  bool was_online = service->online;

  service->ports.close_connection(service->ports.ctx);
  service->online = false;
  service->pending_ticket = 0;
  service->have_active_task = false;
  service->have_last_state = false;
  if (was_online)
    {
      (void)vg_config_service_disconnect(service->config, reason);
    }
  set_error(service, reason);
}

static const char *default_error_code(int ret)
{
  switch (ret)
    {
      case -EALREADY:
        return "too_late";
      case -EBUSY:
        return "busy";
      case -ESTALE:
        return "stale_identity";
      case -ENOENT:
        return "unknown_task";
      case -EACCES:
        return "recovery_blocked";
      case -ENOSPC:
      case -ENOMEM:
        return "capacity_exceeded";
      case -ENOTCONN:
        return "link_lost";
      case -EINVAL:
        return "validation_failed";
      default:
        return "request_failed";
    }
}

static const char *request_path(enum vg_config_mqtt_op_e op)
{
  switch (op)
    {
      case VG_CONFIG_MQTT_QUERY_TASK:
        return "/scope";
      case VG_CONFIG_MQTT_OPEN:
        return "/expected_open_seq";
      case VG_CONFIG_MQTT_PUT:
        return "/revision";
      case VG_CONFIG_MQTT_CANCEL:
        return "/session_id";
      case VG_CONFIG_MQTT_QUERY_DEVICE:
      default:
        return "";
    }
}

static int enqueue_json(struct vg_config_mqtt_service *service,
                        const char *json)
{
  uint16_t ticket = 0;
  int ret;

  if (service->pending_ticket != 0)
    {
      return -EBUSY;
    }
  ret = service->ports.publish(service->ports.ctx, json, &ticket);
  if (ret != 0)
    {
      return ret;
    }
  service->pending_ticket = ticket;
  service->ticket_since = service->ports.now_ms(service->ports.ctx);
  return 0;
}

static int publish_request_error(
  struct vg_config_mqtt_service *service,
  const struct vg_config_mqtt_request *request,
  const struct vg_config_task_status *status,
  int ret,
  const char *detail)
{
  struct vg_config_device_snapshot snapshot;
  char json[VG_CONFIG_STATE_MAX];
  const char *code = default_error_code(ret);
  const char *task_id = NULL;
  const char *session_id = NULL;
  bool retryable = ret == -EAGAIN || ret == -EBUSY || ret == -ENOMEM;

  memset(&snapshot, 0, sizeof(snapshot));
  (void)vg_config_service_snapshot(service->config, &snapshot);
  if (status != NULL && status->error_code[0] != '\0')
    {
      code = status->error_code;
      retryable = status->retryable;
    }
  if (request->task_id[0] != '\0')
    {
      task_id = request->task_id;
    }
  if (request->session_id[0] != '\0')
    {
      session_id = request->session_id;
    }
  if (status != NULL && status->session_id[0] != '\0')
    {
      session_id = status->session_id;
    }

  if (vg_config_mqtt_request_error_json(
        json, sizeof(json), service->device_id, request->req_id,
        snapshot.boot_id, snapshot.link_id, task_id, session_id,
        code, retryable, request_path(request->op),
        detail != NULL ? detail : default_error_code(ret)) != 0)
    {
      return -ENOMEM;
    }
  return enqueue_json(service, json);
}

static int publish_state(struct vg_config_mqtt_service *service,
                         const char *req_id,
                         const struct vg_config_task_status *status)
{
  struct vg_config_device_snapshot snapshot;
  char json[VG_CONFIG_STATE_MAX];
  int ret;

  if (vg_config_service_snapshot(service->config, &snapshot) != 0 ||
      vg_config_mqtt_state_json(
        json, sizeof(json), service->device_id, req_id,
        snapshot.boot_id, snapshot.link_id, status,
        snapshot.uptime_ms, snapshot.time_synced ?
        "synced" : "unsynced") != 0)
    {
      return -ENOMEM;
    }

  ret = enqueue_json(service, json);
  if (ret == 0)
    {
      service->last_state_seq = status->state_seq;
      service->have_last_state = true;
    }
  return ret;
}

static int publish_device_snapshot(
  struct vg_config_mqtt_service *service,
  const struct vg_config_mqtt_request *request)
{
  struct vg_config_device_snapshot snapshot;
  char json[VG_CONFIG_STATE_MAX];
  int ret;

  ret = vg_config_service_snapshot(service->config, &snapshot);
  if (ret != 0)
    {
      return ret;
    }
  if (vg_config_mqtt_device_snapshot_json(
        json, sizeof(json), service->device_id, request->req_id,
        &snapshot) != 0)
    {
      return -ENOMEM;
    }
  return enqueue_json(service, json);
}

static int dispatch_request(struct vg_config_mqtt_service *service,
                            const struct vg_config_mqtt_request *request)
{
  struct vg_config_open_request open_request;
  struct vg_config_put_request put_request;
  struct vg_config_cancel_request cancel_request;
  struct vg_config_task_status status;
  int ret;

  memset(&status, 0, sizeof(status));
  switch (request->op)
    {
      case VG_CONFIG_MQTT_QUERY_DEVICE:
        return publish_device_snapshot(service, request);

      case VG_CONFIG_MQTT_QUERY_TASK:
        ret = vg_config_service_query_task(service->config,
                                           request->task_id, &status);
        if (ret != 0)
          {
            return publish_request_error(service, request, NULL, ret,
                                         "task is not known");
          }
        return publish_state(service, request->req_id, &status);

      case VG_CONFIG_MQTT_OPEN:
        memset(&open_request, 0, sizeof(open_request));
        strcpy(open_request.req_id, request->req_id);
        strcpy(open_request.request_hash, request->request_hash);
        strcpy(open_request.task_id, request->task_id);
        strcpy(open_request.boot_id, request->boot_id);
        strcpy(open_request.link_id, request->link_id);
        open_request.expected_open_seq = request->expected_open_seq;
        open_request.has_base_hash = request->has_base_hash;
        strcpy(open_request.base_hash, request->base_hash);
        ret = vg_config_service_open(service->config, &open_request,
                                     &status);
        if (ret == 0)
          {
            strcpy(service->active_task_id, request->task_id);
            service->have_active_task = true;
            service->have_last_state = false;
            return publish_state(service, request->req_id, &status);
          }
        return publish_request_error(service, request, &status, ret,
                                     "configuration session rejected");

      case VG_CONFIG_MQTT_PUT:
        memset(&put_request, 0, sizeof(put_request));
        strcpy(put_request.req_id, request->req_id);
        strcpy(put_request.request_hash, request->request_hash);
        strcpy(put_request.task_id, request->task_id);
        strcpy(put_request.boot_id, request->boot_id);
        strcpy(put_request.link_id, request->link_id);
        strcpy(put_request.session_id, request->session_id);
        put_request.expected_revision = request->expected_revision;
        put_request.revision = request->revision;
        put_request.has_base_hash = request->has_base_hash;
        strcpy(put_request.base_hash, request->base_hash);
        strcpy(put_request.payload_hash, request->payload_hash);
        put_request.candidate_json = request->candidate_json;
        put_request.candidate_json_len = request->candidate_json_len;
        ret = vg_config_service_put(service->config, &put_request,
                                    &status);
        if (ret == 0)
          {
            return publish_state(service, request->req_id, &status);
          }
        return publish_request_error(service, request, &status, ret,
                                     "candidate rejected");

      case VG_CONFIG_MQTT_CANCEL:
        memset(&cancel_request, 0, sizeof(cancel_request));
        strcpy(cancel_request.req_id, request->req_id);
        strcpy(cancel_request.request_hash, request->request_hash);
        strcpy(cancel_request.task_id, request->task_id);
        strcpy(cancel_request.boot_id, request->boot_id);
        strcpy(cancel_request.link_id, request->link_id);
        strcpy(cancel_request.session_id, request->session_id);
        ret = vg_config_service_cancel(service->config, &cancel_request,
                                       &status);
        if (ret == 0)
          {
            service->have_active_task = false;
            service->have_last_state = false;
            return publish_state(service, request->req_id, &status);
          }
        return publish_request_error(service, request, &status, ret,
                                     "cancel rejected");

      default:
        return -EINVAL;
    }
}

static int publish_local_status(struct vg_config_mqtt_service *service)
{
  if (!service->have_active_task)
    {
      return -ENOENT;
    }

  memset(&service->status, 0, sizeof(service->status));
  if (vg_config_service_query_task(service->config,
                                   service->active_task_id,
                                   &service->status) != 0)
    {
      service->have_active_task = false;
      return -ENOENT;
    }
  if (service->have_last_state &&
      service->status.state_seq == service->last_state_seq)
    {
      return -EAGAIN;
    }

  int ret = publish_state(service, NULL, &service->status);
  if (ret == 0 &&
      service->status.state != VG_CONFIG_SESSION_OPENED &&
      service->status.state != VG_CONFIG_SESSION_VALIDATING &&
      service->status.state != VG_CONFIG_SESSION_AWAITING_CONFIRM &&
      service->status.state != VG_CONFIG_SESSION_COMMITTING)
    {
      service->have_active_task = false;
    }
  return ret;
}

int vg_config_mqtt_service_init(
  struct vg_config_mqtt_service *service,
  struct vg_config_service *config,
  const char *device_id,
  const struct vg_config_mqtt_service_ports *ports)
{
  int ret;

  if (service == NULL || config == NULL || device_id == NULL ||
      strlen(device_id) >= VG_DEVICE_ID_MAX || !ports_valid(ports))
    {
      return -EINVAL;
    }

  memset(service, 0, sizeof(*service));
  ret = pthread_mutex_init(&service->lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }
  ret = pthread_cond_init(&service->changed, NULL);
  if (ret != 0)
    {
      pthread_mutex_destroy(&service->lock);
      return -ret;
    }

  service->config = config;
  service->ports = *ports;
  strcpy(service->device_id, device_id);
  service->initialized = true;
  return 0;
}

int vg_config_mqtt_service_pump(
  struct vg_config_mqtt_service *service)
{
  struct vg_config_mqtt_message *message;
  char error[64];
  int ret;

  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }
  if (!service->ports.ready(service->ports.ctx))
    {
      if (service->online)
        {
          link_lost(service, "network_unavailable");
        }
      return -ENOTCONN;
    }

  if (!service->online)
    {
      ret = vg_config_service_new_link(service->config);
      if (ret != 0)
        {
          set_error(service, "link_identity_failed");
          return ret;
        }
      ret = service->ports.open_connection(service->ports.ctx,
                                            service->device_id);
      if (ret != 0)
        {
          set_error(service, ret == -EKEYREJECTED ?
                    "tls_verification_failed" :
                    ret == -EACCES ? "mqtt_auth_failed" :
                    "mqtt_connect_failed");
          return ret;
        }
      service->online = true;
      service->pending_ticket = 0;
      clear_error(service);
    }

  ret = service->ports.poll_connection(service->ports.ctx);
  if (ret != 0 || !service->ports.online(service->ports.ctx))
    {
      link_lost(service, "link_lost");
      return -ENOTCONN;
    }

  if (service->pending_ticket != 0)
    {
      ret = service->ports.acked(service->ports.ctx,
                                 service->pending_ticket);
      if (ret > 0)
        {
          service->pending_ticket = 0;
        }
      else if (service->ports.now_ms(service->ports.ctx) -
               service->ticket_since >=
               VG_CONFIG_MQTT_ACK_TIMEOUT_MS)
        {
          link_lost(service, "mqtt_puback_timeout");
          return -ETIMEDOUT;
        }
      else
        {
          return 0;
        }
    }

  message = service->ports.peek(service->ports.ctx);
  if (message != NULL)
    {
      ret = vg_config_mqtt_request_parse(
        message->topic, message->payload, message->length,
        service->device_id, true, message->retain, message->qos,
        &service->request, message->payload, sizeof(message->payload),
        error, sizeof(error));
      if (ret == 0)
        {
          ret = dispatch_request(service, &service->request);
          if (ret != 0)
            {
              set_error(service, "state_publish_failed");
            }
          service->ports.release(service->ports.ctx);
          memset(&service->request, 0, sizeof(service->request));
          return ret;
        }
      service->ports.release(service->ports.ctx);
      return 0;
    }

  ret = publish_local_status(service);
  return ret == -ENOENT || ret == -EAGAIN ? 0 : ret;
}

static void *service_thread(void *context)
{
  struct vg_config_mqtt_service *service = context;
  uint64_t next_attempt = 0;
  unsigned backoff = 0;

  for (;;)
    {
      bool stop;

      pthread_mutex_lock(&service->lock);
      stop = service->stopping;
      pthread_mutex_unlock(&service->lock);
      if (stop)
        {
          break;
        }

      uint64_t now = service->ports.now_ms(service->ports.ctx);
      if (now >= next_attempt)
        {
          int ret = vg_config_mqtt_service_pump(service);
          if (ret == 0)
            {
              backoff = 0;
              next_attempt = now;
            }
          else if (!service->online)
            {
              unsigned shift = backoff < 5 ? backoff++ : 5;
              next_attempt = now + (1000u << shift);
            }
        }
      service->ports.delay_ms(service->ports.ctx, 100);
    }

  return NULL;
}

int vg_config_mqtt_service_start(
  struct vg_config_mqtt_service *service)
{
  pthread_attr_t attr;
  int ret;

  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (service->running)
    {
      pthread_mutex_unlock(&service->lock);
      return -EALREADY;
    }
  service->stopping = false;
  service->running = true;
  pthread_mutex_unlock(&service->lock);

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      (void)pthread_attr_setstacksize(&attr,
                                      VG_CONFIG_MQTT_SERVICE_STACK);
      ret = pthread_create(&service->thread, &attr, service_thread,
                           service);
      pthread_attr_destroy(&attr);
    }
  if (ret != 0)
    {
      pthread_mutex_lock(&service->lock);
      service->running = false;
      pthread_mutex_unlock(&service->lock);
      return -ret;
    }
  return 0;
}

int vg_config_mqtt_service_stop(
  struct vg_config_mqtt_service *service)
{
  bool running;

  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  running = service->running;
  service->stopping = true;
  pthread_cond_broadcast(&service->changed);
  pthread_mutex_unlock(&service->lock);
  if (running && pthread_join(service->thread, NULL) != 0)
    {
      return -EIO;
    }
  if (service->online)
    {
      link_lost(service, "service_stopped");
    }
  pthread_mutex_lock(&service->lock);
  service->running = false;
  pthread_mutex_unlock(&service->lock);
  return 0;
}

int vg_config_mqtt_service_status(
  struct vg_config_mqtt_service *service,
  struct vg_config_mqtt_service_status *out)
{
  if (service == NULL || out == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  out->running = service->running;
  out->online = service->online;
  memcpy(out->error, service->error, sizeof(out->error));
  pthread_mutex_unlock(&service->lock);
  return 0;
}
