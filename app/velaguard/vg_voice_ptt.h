/****************************************************************************
 * app/velaguard/vg_voice_ptt.h
 *
 * USER1 按键到本地录放闭环的接线。
 *
 * 与 vg_ptt_board.h 的分工：那一层只把电平变成按下/提交事件，本层决定
 * 这些事件对录音做什么。产品语音界面（浮层、ASR、TTS）不在本层。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_VOICE_PTT_H
#define __VELAGUARD_VG_VOICE_PTT_H

/**
  * @brief  初始化闭环并启动按键任务，事件接到录音与回放。
  * @note   幂等。回调必须在按键任务创建前注册，因此本函数自己完成
  *         vg_voice_init 与 vg_ptt_start，调用者不要再单独启动按键任务。
  * @retval 0  已就绪。
  * @retval <0 初始化或启动失败。
  */
int vg_voice_ptt_start(void);

#endif /* __VELAGUARD_VG_VOICE_PTT_H */
