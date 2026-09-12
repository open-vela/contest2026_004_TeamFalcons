/****************************************************************************
 * VelaGuard ai_agent seed content (eMMC /data/agent).
 *
 * Writes default Skill markdown once; applies encrypted LLM provision from eMMC.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char g_modbus_query_skill[] =
"# Modbus 实时查数（VelaGuard）\n"
"\n"
"## 何时使用\n"
"\n"
"用户问从站寄存器、温湿度、水浸、通信质量时，用 **run_shell** 调 NSH 只读工具。\n"
"\n"
"## 命令\n"
"\n"
"- 读保持寄存器：`vgmodbus -a <addr> -r <reg> -c <count> -n 1 -i 0`\n"
"- 帧统计：`vgstats dump <addr>`\n"
"- 配置摘要：`vgcfg dump`\n"
"\n"
"## 示例\n"
"\n"
"从站 1 温湿度 reg0..1：\n"
"```\n"
"vgmodbus -a 1 -r 0 -c 2 -n 1 -i 0\n"
"```\n"
"\n"
"禁止写操作、禁止 mkfs/reboot。只读。\n"
"禁止调用 vgpoint、vgdiscover apply、vgcfg commit。\n";

static const char g_alarm_interpretation_skill[] =
"# 告警解释（VelaGuard）\n"
"\n"
"## 何时使用\n"
"\n"
"用户问告警含义、/data/velaguard/pending_alarm.txt 有内容、或 HEARTBEAT 指派告警任务时。\n"
"\n"
"## 步骤\n"
"\n"
"1. read_file /data/velaguard/pending_alarm.txt（若存在）\n"
"2. run_shell `vgstats dump <slave>` 查通信质量\n"
"3. run_shell `vgmodbus -a <slave> -r 0 -c 4 -n 1 -i 0` 查实时值\n"
"4. run_shell `vgcfg dump` 了解设备名/配置\n"
"5. 输出纯文本解释：摘要、证据、建议关注各一段，逐行短句\n"
"   禁止 Markdown 语法（不用 #、*、|、表格、代码块）\n"
"6. write_file /data/velaguard/reports/last_alarm.md 保存解释\n"
"\n"
"## 约束\n"
"\n"
"- 只读；禁止写寄存器、改阈值、mkfs、reboot\n"
"- 禁止 run_shell：vgpoint、vgdiscover apply、vgcfg commit\n"
"- 信息不足时设 unresolved=true，不要编造点位\n";

static const char g_operations_report_skill[] =
"# 运营日报（VelaGuard）\n"
"\n"
"## 何时使用\n"
"\n"
"用户要日报/运营报告，或 HEARTBEAT 要求生成 daily 报告时。\n"
"\n"
"## 步骤\n"
"\n"
"0. read_file /data/agent/skills/operations_report.md（按本 Skill 执行，勿 list_dir）\n"
"1. get_current_time 取日期\n"
"2. run_shell `vgstats dump 1`（及已配置从站）\n"
"3. run_shell `vgmodbus -a 1 -r 0 -c 4 -n 1 -i 0` 采样关键寄存器\n"
"4. run_shell `vgcfg dump`\n"
"5. 汇总为纯文本：告警摘要、通信质量、关键指标、建议关注\n"
"   禁止 Markdown 语法（不用 #、*、|、表格、代码块），用普通行文和编号\n"
"6. write_file /data/velaguard/reports/daily-YYYYMMDD.md\n"
"\n"
"## 约束\n"
"\n"
"- 指标必须来自 run_shell 输出，禁止编造\n"
"- 只读；禁止写 Modbus/配置\n"
"- 禁止 run_shell：vgpoint、vgdiscover apply、vgcfg commit\n";

static const char g_heartbeat_md[] =
"# VelaGuard Heartbeat\n"
"\n"
"周期性主动任务（只读）：\n"
"\n"
"- [ ] 若 /data/velaguard/pending_alarm.txt 存在：按 alarm_interpretation Skill "
"解释，写入 /data/velaguard/reports/last_alarm.md，完成后删除 pending 文件\n"
"- [ ] 若今日 /data/velaguard/reports/daily-YYYYMMDD.md 尚不存在：按 "
"operations_report Skill 生成日报（YYYYMMDD 用 get_current_time）\n"
"\n"
"工具仅限 run_shell：vgmodbus、vgstats、vgcfg dump、vgnet。\n"
"禁止 vgpoint、vgdiscover apply、vgcfg commit。\n";

static int write_seed_file(const char *path, const char *body)
{
  size_t len = strlen(body);
  bool same = false;
  struct stat st;
  int fd;

  /* Refresh in place when eMMC already holds an older seed text, so
   * firmware-side wording changes land on previously provisioned boards. */

  fd = open(path, O_RDONLY);
  if (fd >= 0)
    {
      if (fstat(fd, &st) == 0 && st.st_size == (off_t)len)
        {
          char chunk[128];
          size_t off;

          same = true;
          for (off = 0; off < len && same; off += sizeof(chunk))
            {
              size_t want = len - off;
              ssize_t got;

              if (want > sizeof(chunk))
                {
                  want = sizeof(chunk);
                }

              got = read(fd, chunk, want);
              if (got != (ssize_t)want ||
                  memcmp(chunk, body + off, want) != 0)
                {
                  same = false;
                }
            }
        }

      close(fd);
      if (same)
        {
          return 0;
        }
    }

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }

  if (write(fd, body, len) != (ssize_t)len)
    {
      close(fd);
      return -EIO;
    }

  close(fd);
  return 0;
}

static void wait_for_data_mount(void)
{
  struct stat st;
  int i;

  for (i = 0; i < 50; i++)
    {
      if (stat("/data", &st) == 0)
        {
          return;
        }

      usleep(100000);
    }
}

void vg_agent_seed_content(void)
{
  wait_for_data_mount();
  (void)write_seed_file("/data/agent/skills/modbus_query.md",
                          g_modbus_query_skill);
  /* LLM config: run `vgprovision apply` after encrypted provision (not at boot). */

#ifdef CONFIG_VG_AGENT_OPS
  (void)mkdir("/data/velaguard/reports", 0755);
  (void)write_seed_file("/data/agent/skills/alarm_interpretation.md",
                          g_alarm_interpretation_skill);
  (void)write_seed_file("/data/agent/skills/operations_report.md",
                          g_operations_report_skill);
  (void)write_seed_file("/data/agent/HEARTBEAT.md", g_heartbeat_md);
#endif
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */
