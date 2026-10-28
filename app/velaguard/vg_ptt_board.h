/****************************************************************************
 * app/velaguard/vg_ptt_board.h
 *
 * USER1 按住说话的板端适配层：打开 /dev/buttons，阻塞等待按键事件，
 * 把电平喂给 vg_ptt 状态机，并把开始/提交事件交给上层。
 *
 * 工作单元只在三种情况下醒来：按键驱动通知事件、状态机给出到期时刻
 * （去抖确认、15 秒上限）、或停止请求写入自管道。没有固定周期的轮询。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_PTT_BOARD_H
#define __VELAGUARD_VG_PTT_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "vg_ptt.h"

/* 上层回调：state 机产生事件时在 ptt 线程里调用。
 * 当前没有音频层，默认实现只打印；BVO-03/05 接入录音后替换。
 */

typedef void (*vg_ptt_event_fn)(const struct vg_ptt_event_record *rec,
                                void *arg);

/**
  * @brief  一次运行统计。用于 NSH 诊断与板端按放验收。
  * @note   begins 与 submits 都只统计状态机产生的事件；samples 是读取
  *         /dev/buttons 的次数，用来区分卡死与没有按键。
  */
struct vg_ptt_stats
{
  bool     running;       /**< 线程是否在运行 */
  bool     opened;        /**< /dev/buttons 是否已打开 */
  uint32_t begins;        /**< 受理的按下次数 */
  uint32_t submits;       /**< 提交次数 */
  uint32_t timeouts;      /**< 其中由 15 秒上限触发 */
  uint32_t samples;       /**< 按键读取次数 */
  uint32_t raw_highs;     /**< 其中读到按下的次数（未经去抖） */
  uint32_t wake_events;   /**< poll 报告的事件次数 */
  uint32_t wake_timeouts; /**< poll 超时次数（按到期时刻醒来） */
  uint32_t poll_errors;   /**< poll 失败次数 */
  uint32_t read_errors;   /**< read 失败次数 */
  uint32_t last_duration_ms; /**< 最近一次提交的录音时长 */
  enum vg_ptt_state state;   /**< 状态机当前状态 */
  uint32_t last_event_ms;    /**< 最近一次事件或样本的单调毫秒 */
};

/**
  * @brief  启动按键任务（幂等）。
  * @note   工作单元是 NuttX task 而不是 pthread：NSH 命令跑在自己任务组
  *         里，命令返回会回收该组，组内 pthread 也跟着消失。
  * @param  devpath  /dev/buttons；传 NULL 用默认值。
  * @param  fn       事件回调；NULL 时用内置打印。
  * @param  arg      回调参数。
  * @retval 0  已启动或已在运行。
  * @retval <0 创建任务或唤醒管道失败；设备打开失败在任务内报告。
  */
int vg_ptt_start(const char *devpath, vg_ptt_event_fn fn, void *arg);

/**
  * @brief  请求停止并等待线程退出。
  * @note   停止通过自管道打断 poll，空闲时也能立即生效。线程退出后写端
  *         才关闭，读端由线程自己关闭。
  * @param  wait_ms  最多等待线程退出的毫秒数；<=0 用默认 2000。
  * @retval 0  已停止或本来就没运行。
  * @retval -1 线程没有在给定时间内退出。
  */
int vg_ptt_stop(int wait_ms);

/** @brief  读取统计快照。任意线程可调用。 */
void vg_ptt_get_stats(struct vg_ptt_stats *out);

/** @brief  清零计数，不影响状态机。 */
void vg_ptt_reset_stats(void);

/** @brief  当前状态机快照（诊断用）。 */
void vg_ptt_get_state(struct vg_ptt *out);

/**
  * @brief  读一次按键电平，不动状态机、不计数。
  * @note   用于把"电平没变"和"中断没送达"分开：如果按键时这里一直读到
  *         松开值，问题在引脚或按键电路；如果这里随按键变化但事件计数
  *         不动，问题在中断或 poll 通路。
  * @param  pressed  输出：是否读到按下。
  * @retval 0   读取成功。
  * @retval <0  设备打不开或读取失败。
  */
int vg_ptt_probe_level(bool *pressed);

#endif /* __VELAGUARD_VG_PTT_BOARD_H */
