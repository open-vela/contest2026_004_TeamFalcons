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
"- 运行累计：`vgruntime dump`\n"
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
"# 告警建议（VelaGuard）\n"
"\n"
"## 何时使用\n"
"\n"
"收到带 boot= 与 req= 的活动告警建议请求时，或用户问告警含义时。\n"
"\n"
"## 步骤（两步）\n"
"\n"
"1. 在**同一条消息**里一次发出取证命令，它们互不依赖：\n"
"   run_shell vgstats dump（不加参数，全部从站）\n"
"   run_shell vgmodbus -a <从站号> -r 0 -c 4 -n 1 -i 0\n"
"   run_shell vgcfg dump\n"
"2. write_file /data/velaguard/reports/alarm_advice.txt\n"
"\n"
"每条消息里的工具调用会被一次执行完。一条消息只发一个调用就要多等一整轮，\n"
"而单次模型调用有 120 秒上限，所以取证要一次发齐、不要逐个试探。\n"
"\n"
"告警的点位、数值、阈值和持续时间都已经写在请求里，直接把请求当数据源，\n"
"不要为了取告警去 read_file /data/velaguard/pending_alarm.txt。\n"
"单独一次 read_file 会被板端当成取文件内容作答复并提前结束本轮，后面的\n"
"write_file 就不会执行。确实需要读文件时，把 read_file 和至少一个 run_shell\n"
"写在同一条消息里一起调用。\n"
"\n"
"## 输出格式（逐行，顺序不可变，行尾不要空格）\n"
"\n"
"VGADV1\n"
"boot=<原样抄请求里的 boot>\n"
"req=<原样抄请求里的 req>\n"
"n=<条数>\n"
"[1]\n"
"id=<原样抄请求里的 id>\n"
"epoch=<原样抄请求里的 epoch>\n"
"sev=<原样抄请求里的 sev>\n"
"unres=0 或 1\n"
"sum=<一句话建议，不超过 24 个汉字>\n"
"ev=<判断依据，不超过 60 个汉字>\n"
"att=<建议关注，不超过 60 个汉字>\n"
"[2]\n"
"（第 2 条起同样 7 行格式，序号递增；id、epoch、sev 必须与请求逐条对应）\n"
"END\n"
"\n"
"## 约束\n"
"\n"
"- 只写 /data/velaguard/reports/alarm_advice.txt，不要动 runtime-report.md\n"
"- boot 与 req 必须原样回填，写错会让整份被板端丢弃\n"
"- 全部使用中文常用字，不要用 Markdown、表格或代码块\n"
"- 数字只能来自工具输出，信息不足时 unres=1 并在 sum 里说明，不要编造\n"
"- 禁止 list_dir、ls、cat\n"
"- 禁止 vgpoint、vgdiscover apply、vgcfg commit\n";

static const char g_operations_report_skill[] =
"# 运行日报（VelaGuard）\n"
"\n"
"## 何时使用\n"
"\n"
"收到生成今日运行日报的请求时，或用户要口述运行概况时。只写日报，不写周报。\n"
"\n"
"## 必做步骤（两步，工具调用不超过 4 次）\n"
"\n"
"1. 在**同一条消息**里一次发出这三个调用，它们互不依赖：\n"
"   get_current_time\n"
"   run_shell vgruntime dump\n"
"   run_shell vgstats dump（不加参数，全部从站）\n"
"2. write_file，路径用请求里给出的那一个\n"
"\n"
"每条消息里的工具调用会被一次执行完，一条消息发一个调用就要多等一整轮，\n"
"而单次模型调用有 120 秒上限。把第 1 步的三条并在一条消息里是本 Skill 的硬要求。\n"
"\n"
"## 文件格式（纯文本，禁止 Markdown 标记）\n"
"\n"
"第一行：AI-DAILY v1\n"
"第二行：date=<YYYY-MM-DD>\n"
"第三行：source=agent\n"
"第四行：---\n"
"随后固定三节，每节一行标题加两到四行正文：\n"
"通信质量 / 点位在线 / 异常时间线\n"
"正文总长不超过 1400 字节，全部使用中文常用字。\n"
"\n"
"## 约束\n"
"\n"
"- 只写 daily-<当天日期>.md，不得写 runtime-report.md\n"
"- 数字只能来自工具输出，不得编造；某项取不到时该节写数据不足\n"
"- 禁止 list_dir、ls、cat\n"
"- 禁止 vgpoint、vgdiscover apply、vgcfg commit\n";

static const char g_heartbeat_md[] =
"# VelaGuard Heartbeat\n"
"\n"
"HMI 构建下心跳线程不再直接发起模型轮次。告警建议与日报都由板端文件\n"
"工作线程经本地 IPC 发起，本文件只作现场说明。\n"
"\n"
"禁止 list_dir、ls、cat。不要写 runtime-report.md（那是固件的离线兜底）。\n"
"收到带 boot= 与 req= 的请求时按 alarm_interpretation 处理，不要主动去读\n"
"/data/velaguard/pending_alarm.txt 找活干。\n"
"\n"
"工具仅限：vgstats dump、vgmodbus、vgruntime dump、vgcfg dump、vgnet status、\n"
"get_current_time、read_file、write_file。\n"
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
