/****************************************************************************
 * app/velaguard/vg_ptt.h
 *
 * USER1 按住说话（push-to-talk）事件状态机。
 *
 * 输入是按键的瞬时电平与单调时钟；输出只有三个事件：开始录音、提交、无。
 * 去抖、15 秒上限、松手门控都在这里判定，不依赖 NuttX 头文件，可直接在主机
 * 上跑时序用例。
 *
 * 约束（执行方案第 2 节）：
 *   - 接受一次按下后开始录音，松手提交；
 *   - 按住达到 15 秒自动提交，此后必须等松手才接受下一次按下；
 *   - 抖动不产生新会话，持续按住不重复提交。
 *
 * 时间用 uint32 毫秒，只做无符号相减，可跨 2^32 回绕；单次间隔须小于
 * 2^31 毫秒（约 24.8 天）。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_PTT_H
#define __VELAGUARD_VG_PTT_H

#include <stdbool.h>
#include <stdint.h>

/* 按住的硬上限：到点自动提交，录音层按同一上限截断 */
#define VG_PTT_MAX_HOLD_MS   15000u

/* 电平连续稳定的判定窗口；短于它的跳变不产生事件 */
#define VG_PTT_DEBOUNCE_MS   20u

enum vg_ptt_state
{
  VG_PTT_IDLE = 0,   /* 等按下 */
  VG_PTT_RECORDING,  /* 已受理按下，录音中 */
  VG_PTT_HELD        /* 到上限已提交，等松手 */
};

enum vg_ptt_submit_reason
{
  VG_PTT_SUBMIT_RELEASE = 0, /* 用户松手 */
  VG_PTT_SUBMIT_TIMEOUT      /* 15 秒上限 */
};

enum vg_ptt_event
{
  VG_PTT_EV_NONE = 0,
  VG_PTT_EV_BEGIN,
  VG_PTT_EV_SUBMIT
};

/**
  * @brief  一次事件的结果。
  * @note   press_ms 是受理按下时的单调毫秒；duration_ms 仅提交事件有效，
  *         松手提交按各自的边沿时刻计算，上限提交固定为 VG_PTT_MAX_HOLD_MS。
  */
struct vg_ptt_event_record
{
  enum vg_ptt_event          type;
  enum vg_ptt_submit_reason  reason;
  uint32_t                   press_ms;
  uint32_t                   duration_ms;
};

/** @brief  上次 update 调用时按键电平的稳定值，raw 为尚未确认的瞬时值。 */
struct vg_ptt
{
  enum vg_ptt_state state;
  bool              raw_level;      /* 最近一次采样到的电平 */
  bool              stable_level;   /* 已确认的电平 */
  bool              started;        /* init 是否已执行 */
  bool              ungated;        /* 已见过一次确认的松手，可以接受按下 */
  uint32_t          edge_ms;        /* 最近一次瞬时电平变化的时刻 */
  uint32_t          press_ms;       /* 受理按下的时刻（取按下边沿） */
};

/**
  * @brief  用当前电平初始化状态机。
  * @note   上电时已按住不产生事件：初始电平直接作为已确认值，等松手后
  *         的下一次按下才算。
  * @param  ptt      状态机。
  * @param  level    true=按下。
  * @param  now_ms   单调毫秒。
  * @retval None
  */
void vg_ptt_init(struct vg_ptt *ptt, bool level, uint32_t now_ms);

/**
  * @brief  喂入一次电平采样，推进状态机。
  * @note   同一时刻最多产生一个事件；录音中先判电平确认再判上限，因此松手
  *         边沿落在 15 秒整点时按松手提交，不会重复提交。
  * @param  ptt      状态机。
  * @param  level    当前电平。
  * @param  now_ms   单调毫秒。
  * @param  rec      输出事件；无事件时 type 置 VG_PTT_EV_NONE。可为 NULL。
  * @retval 产生的事件类型，与 rec->type 相同。
  */
enum vg_ptt_event vg_ptt_update(struct vg_ptt *ptt, bool level,
                                uint32_t now_ms,
                                struct vg_ptt_event_record *rec);

/**
  * @brief  距离下一次必须处理的时刻还有多少毫秒。
  * @note   调用方据此设置等待超时，不需要按固定周期轮询。没有待处理事项时
  *         返回 -1，表示可以无限等待。
  * @param  ptt     状态机。
  * @param  now_ms  单调毫秒。
  * @retval >=0 毫秒；-1 无到期事项。
  */
int vg_ptt_next_timeout_ms(const struct vg_ptt *ptt, uint32_t now_ms);

/** @brief  状态名（诊断输出用）。 */
const char *vg_ptt_state_name(enum vg_ptt_state state);

/** @brief  提交原因名（诊断输出用）。 */
const char *vg_ptt_reason_name(enum vg_ptt_submit_reason reason);

#endif /* __VELAGUARD_VG_PTT_H */
