/****************************************************************************
 * app/velaguard/vg_sntp.h
 *
 * SNTP 报文与墙钟换算纯函数。不依赖 NuttX 头文件，host_tests 直接编译。
 ****************************************************************************/

#ifndef __VELAGUARD_VG_SNTP_H
#define __VELAGUARD_VG_SNTP_H

#include <stdbool.h>
#include <stdint.h>

/** NTP 纪元 1900-01-01 与 Unix 纪元 1970-01-01 的秒差 */
#define VG_SNTP_ERA_OFFSET 2208988800ull

/** 北京时间 = UTC + 8h；系统时钟统一跑北京时间墙钟 */
#define VG_TIME_TZ_OFFSET_SEC (8 * 3600)

/** 墙钟合法范围（UTC 秒）：2020-01-01 .. 2100-01-01 */
#define VG_TIME_WALL_MIN 1577836800ll
#define VG_TIME_WALL_MAX 4102444800ll

/**
  * @brief  组一个 48 字节 SNTPv4 客户端查询报文。
  * @param  buf  输出缓冲，恰好 48 字节。
  * @retval 0    成功。
  * @retval -1   参数非法。
  */
int vg_sntp_build_query(uint8_t buf[48]);

/**
  * @brief  解析 SNTP 服务器应答，取 Transmit Timestamp 换算 Unix UTC 秒。
  * @note   拒绝 LI=3（告警/ KoD）、Mode 非 server/broadcast、早于 1970。
  * @param  buf       应答缓冲，至少 48 字节。
  * @param  utc_out   输出 Unix UTC 秒。
  * @retval 0          成功。
  * @retval -1         报文非法或参数为空。
  */
int vg_sntp_parse_response(const uint8_t buf[48], int64_t *utc_out);

/**
  * @brief  Unix UTC 秒换算北京时间墙钟秒（即 UTC+8 后的 clock 值）。
  */
int64_t vg_time_utc_to_wall(int64_t utc_sec);

/**
  * @brief  墙钟值是否在 2020..2100 年范围内（恢复/持久化的护栏）。
  */
bool vg_time_wall_plausible(int64_t wall_sec);

#endif /* __VELAGUARD_VG_SNTP_H */
