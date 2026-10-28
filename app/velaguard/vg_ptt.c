/****************************************************************************
 * app/velaguard/vg_ptt.c
 *
 * USER1 按住说话事件状态机，纯逻辑，无 NuttX 依赖。
 * 接口与时序说明见 vg_ptt.h。
 ****************************************************************************/

#include "vg_ptt.h"

#include <stddef.h>

/**
  * @brief  两个单调毫秒时刻的差值，按无符号相减以容忍回绕。
  * @param  now   较晚时刻。
  * @param  then  较早时刻。
  * @retval 毫秒差。
  */
static uint32_t since(uint32_t now, uint32_t then)
{
  return now - then;
}

/**
  * @brief  距离 due 还有多久，已过期返回 0。
  * @note   用有符号差判断先后；单次间隔须小于 2^31 毫秒。
  * @param  due_ms  到期时刻。
  * @param  now_ms  当前时刻。
  * @retval 0 或剩余毫秒。
  */
static uint32_t wait_until(uint32_t due_ms, uint32_t now_ms)
{
  int32_t delta = (int32_t)(due_ms - now_ms);

  return (delta <= 0) ? 0u : (uint32_t)delta;
}

void vg_ptt_init(struct vg_ptt *ptt, bool level, uint32_t now_ms)
{
  if (ptt == NULL)
    {
      return;
    }

  /* 上电即按住不当成一次录音：把当前电平直接当作已确认值，并且要求先
   * 见到一次确认的松手才解锁按下。否则按下边沿早已过去，却会在第一次
   * 更新时被当成新会话。 */

  ptt->state = VG_PTT_IDLE;
  ptt->raw_level = level;
  ptt->stable_level = level;
  ptt->started = true;
  ptt->ungated = !level;
  ptt->edge_ms = now_ms;
  ptt->press_ms = 0;
}

enum vg_ptt_event vg_ptt_update(struct vg_ptt *ptt, bool level,
                                uint32_t now_ms,
                                struct vg_ptt_event_record *rec)
{
  struct vg_ptt_event_record scratch;
  enum vg_ptt_event event = VG_PTT_EV_NONE;
  bool submit = false;

  if (rec == NULL)
    {
      rec = &scratch;
    }

  rec->type = VG_PTT_EV_NONE;
  rec->reason = VG_PTT_SUBMIT_RELEASE;
  rec->press_ms = 0;
  rec->duration_ms = 0;

  if (ptt == NULL)
    {
      return VG_PTT_EV_NONE;
    }

  if (!ptt->started)
    {
      vg_ptt_init(ptt, level, now_ms);
      return VG_PTT_EV_NONE;
    }

  /* 记录未确认的电平跳变。边沿时刻在这里存档，确认后按边沿而不是按确认
   * 时刻计时，这样时长与用户实际按住的时间一致。 */

  if (level != ptt->raw_level)
    {
      ptt->raw_level = level;
      ptt->edge_ms = now_ms;
    }

  /* 电平保持稳定达到去抖窗口才确认；短于窗口的跳变被吞掉。 */

  if (ptt->raw_level != ptt->stable_level &&
      since(now_ms, ptt->edge_ms) >= VG_PTT_DEBOUNCE_MS)
    {
      ptt->stable_level = ptt->raw_level;
    }

  switch (ptt->state)
    {
      case VG_PTT_IDLE:
        if (!ptt->stable_level)
          {
            /* 已确认的松手：此后的按下边沿才是新会话的起点。 */

            ptt->ungated = true;
          }
        else if (ptt->ungated)
          {
            ptt->state = VG_PTT_RECORDING;
            ptt->press_ms = ptt->edge_ms;
            event = VG_PTT_EV_BEGIN;
          }

        break;

      case VG_PTT_RECORDING:

        /* 先判用户松手，再判上限：松手边沿正好落在 15 秒整点时按松手
         * 提交，不会同一轮再补一次超时提交。
         *
         * 电平尚未确认时按 raw 判断：用户在去抖窗口内已经松手，说明这一
         * 次不是长按，不能因为等待确认就超出上限自动提交。 */

        if (!ptt->raw_level)
          {
            if (!ptt->stable_level)
              {
                ptt->state = VG_PTT_IDLE;
                ptt->ungated = true;
                submit = true;
              }
          }
        else if (since(now_ms, ptt->press_ms) >= VG_PTT_MAX_HOLD_MS)
          {
            ptt->state = VG_PTT_HELD;
            submit = true;
          }

        break;

      case VG_PTT_HELD:

        /* 已按上限提交，只等松手，期间不再产生事件。 */

        if (!ptt->stable_level)
          {
            ptt->state = VG_PTT_IDLE;
            ptt->ungated = true;
          }

        break;
    }

  if (submit)
    {
      event = VG_PTT_EV_SUBMIT;
      rec->press_ms = ptt->press_ms;

      if (ptt->state == VG_PTT_HELD)
        {
          rec->reason = VG_PTT_SUBMIT_TIMEOUT;
          rec->duration_ms = VG_PTT_MAX_HOLD_MS;
        }
      else
        {
          rec->reason = VG_PTT_SUBMIT_RELEASE;
          rec->duration_ms = since(ptt->edge_ms, ptt->press_ms);
        }
    }
  else if (event == VG_PTT_EV_BEGIN)
    {
      rec->press_ms = ptt->press_ms;
    }

  rec->type = event;
  return event;
}

int vg_ptt_next_timeout_ms(const struct vg_ptt *ptt, uint32_t now_ms)
{
  uint32_t best = 0;
  bool have = false;

  if (ptt == NULL || !ptt->started)
    {
      return -1;
    }

  /* 电平跳变待确认：去抖窗口到期时回来确认 */

  if (ptt->raw_level != ptt->stable_level)
    {
      best = wait_until(ptt->edge_ms + VG_PTT_DEBOUNCE_MS, now_ms);
      have = true;
    }

  /* 录音中：上限到期时回来提交 */

  if (ptt->state == VG_PTT_RECORDING)
    {
      uint32_t cap = wait_until(ptt->press_ms + VG_PTT_MAX_HOLD_MS, now_ms);

      if (!have || cap < best)
        {
          best = cap;
        }

      have = true;
    }

  return have ? (int)best : -1;
}

const char *vg_ptt_state_name(enum vg_ptt_state state)
{
  switch (state)
    {
      case VG_PTT_IDLE:
        return "idle";

      case VG_PTT_RECORDING:
        return "recording";

      case VG_PTT_HELD:
        return "held";

      default:
        return "?";
    }
}

const char *vg_ptt_reason_name(enum vg_ptt_submit_reason reason)
{
  switch (reason)
    {
      case VG_PTT_SUBMIT_RELEASE:
        return "release";

      case VG_PTT_SUBMIT_TIMEOUT:
        return "timeout";

      default:
        return "?";
    }
}
