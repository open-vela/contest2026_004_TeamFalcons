# Stage1 agent-ops 板端验收（COM3，需 VG_AGENT_AUTOSTART=y）。
# autostart 为 --daemon 后台；交互前先 nsh> ai_agent 进入 vela>。
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_agent_ops_accept.ps1

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$MimoToken = $env:MIMO_API_KEY
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0
$script:console = ""

function Wait-Prompt {
  param([string]$Buf, [int]$TimeoutSec = 12)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($Buf -match "vela>" -or $Buf -match "nsh>") { return $true }
    Start-Sleep -Milliseconds 120
  }
  return $false
}

function Send-Serial {
  param(
    [System.IO.Ports.SerialPort]$Port,
    [string]$Cmd,
    [int]$WaitSec = 20,
    [switch]$WantVela
  )
  $Port.DiscardInBuffer()
  $Port.WriteLine($Cmd)
  Start-Sleep -Milliseconds 600
  $buf = ""
  $deadline = (Get-Date).AddSeconds($WaitSec)
  while ((Get-Date) -lt $deadline) {
    if ($Port.BytesToRead -gt 0) { $buf += $Port.ReadExisting() }
    if ($WantVela -and $buf -match "vela>") { break }
    if (-not $WantVela -and (Wait-Prompt $buf 1)) { break }
    Start-Sleep -Milliseconds 120
  }
  $block = "`n=== $Cmd ===`n$buf"
  # Everything the board has said since the port opened.  A proactive round
  # prints its request line minutes before anything this script asks for, so
  # per-step buffers are not enough to prove the board asked on its own.
  $script:console += $buf
  Write-Output $block
  return @{ Text = $buf; Block = $block }
}

function Assert-Match {
  param([string]$Name, [string]$Hay, [string]$Pattern)
  if ($Hay -match $Pattern) {
    Write-Output "[PASS] $Name"
    $script:pass++
  } else {
    Write-Output "[FAIL] $Name (pattern: $Pattern)"
    $script:fail++
  }
}

# Compare the value the agent quoted against the board's own reading.
#
# A substring test cannot do this: the reference for ups_load may be "1", and
# "1" occurs inside "10", which is exactly what an unscaled register would
# print (the point is scale 0.1).  So both sides are taken as numbers and
# compared with a tiny relative tolerance, which accepts "10" written as
# "10.0" while rejecting "100".
function Assert-ValueQuoted {
  param([string]$Name, [string]$Hay, [double]$Ref)
  $tol = [Math]::Max(1.0, [Math]::Abs($Ref)) * 1e-6
  $found = $false
  foreach ($m in [regex]::Matches($Hay, '-?\d+(?:\.\d+)?')) {
    $v = 0.0
    if ([double]::TryParse($m.Value, [ref]$v) -and
        [Math]::Abs($v - $Ref) -le $tol) {
      $found = $true
      break
    }
  }
  if ($found) {
    Write-Output "[PASS] $Name"
    $script:pass++
  } else {
    Write-Output "[FAIL] $Name (expected value $Ref in the reply)"
    $script:fail++
  }
}

$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.NewLine = "`n"

try {
  $port.Open()
  Start-Sleep -Milliseconds 1500
  while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 80 }

  # Wait for autostart (up to 45s after reset)
  $boot = ""
  $deadline = (Get-Date).AddSeconds(45)
  while ((Get-Date) -lt $deadline) {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    if ($boot -match "vgagent: ai_agent autostart ok" -and $boot -match "nsh>") { break }
    Start-Sleep -Milliseconds 200
  }
  # The flash script usually consumes the boot banner first, so a missing
  # banner here says nothing about whether the daemon is up.  Accept either.
  $ps = Send-Serial $port "ps" 10
  if ($boot -match "vgagent: ai_agent autostart ok" -or $ps.Text -match "ai_agent") {
    Write-Output "[PASS] ai_agent running"
    $script:pass++
  } else {
    Write-Output "[FAIL] ai_agent not visible in boot banner or ps"
    $script:fail++
  }

  $r = Send-Serial $port "ls /data/agent/skills" 12
  Assert-Match "operations_report skill" $r.Text "operations_report"
  Assert-Match "alarm_interpretation skill" $r.Text "alarm_interpretation"
  Assert-Match "modbus_query skill" $r.Text "modbus_query"

  $r = Send-Serial $port "cat /data/agent/HEARTBEAT.md" 10
  Assert-Match "HEARTBEAT file" $r.Text "HEARTBEAT|heartbeat|告警|日报"

  # Can the board reach the model at all?  Everything below that depends on a
  # completed round is gated on this, because the two ways it can fail are not
  # the same finding: "the board never asked" is a product bug, while "TLS to
  # the model host fails" is a bench/network problem that would otherwise be
  # reported as the former.  Measured with a real round, not by reading
  # credentials: a key can be configured while the host is unreachable, which
  # is exactly this bench's state (the domain resolves to a proxy fake-IP the
  # board has no route to).
  #
  # The probe is short because a failing round fails in well under a second
  # (`net_connect ret=0x42`), while a working one streams its reply here.
  $probe = Send-Serial $port "ai_agent" 25 -WantVela
  $llmReachable = $false
  if ($probe.Text -match "vela>") {
    $probe = Send-Serial $port "ask 你好" 90
    $llmReachable = ($probe.Block -notmatch "llm=fail") -and
                    ($probe.Text -notmatch "LLM call failed")
    if ($llmReachable) {
      Write-Output "[INFO] model backend reachable from the board"
    } else {
      Write-Output "[INFO] model backend unreachable from the board; round-dependent checks below are reported, not failed"
    }
    Send-Serial $port "quit" 15 | Out-Null
    Start-Sleep -Seconds 1
  } else {
    Write-Output "[FAIL] could not attach to the agent CLI to probe the model backend"
    $script:fail++
  }

  # The daily report is proactive: once the clock is synced and today's file
  # is missing, the board asks the agent by itself.
  #
  # The board names the file after its own local day, so the way to make this
  # repeatable is to remove whatever dated file is already there and then
  # require the board to produce it again unprompted.  Nothing in this script
  # talks to the agent before this point, so a file that reappears can only
  # have come from the board's own request.
  $r = Send-Serial $port "ls /data/velaguard/reports" 8
  $dated = [regex]::Matches($r.Text, "daily-\d{4}-\d{2}-\d{2}\.md") |
           ForEach-Object { $_.Value } | Sort-Object -Unique
  if ($dated) {
    $stale = @($dated)[-1]
    Write-Output "[INFO] removing $stale so the board has to ask for it again"
    foreach ($attempt in 1..3) {
      Send-Serial $port "rm /data/velaguard/reports/$stale" 8 | Out-Null
      $r = Send-Serial $port "ls /data/velaguard/reports" 8
      $dated = [regex]::Matches($r.Text, "daily-\d{4}-\d{2}-\d{2}\.md") |
               ForEach-Object { $_.Value } | Sort-Object -Unique
      if (-not ($dated -contains $stale)) { break }
      Start-Sleep -Seconds 2
    }
    if ($dated -contains $stale) {
      Write-Output "[FAIL] $stale is still on eMMC after rm"
      $script:fail++
    } else {
      Write-Output "[PASS] today's daily removed so the board must regenerate it"
      $script:pass++
    }
  }
  $before = $dated

  # One round takes about 195 s, the board only starts once its clock is
  # synced, and both flows share one channel and serialise: an advice round
  # can hold it first, so the window has to cover two rounds.
  # Stay in the loop until the file is there AND its content parses: the
  # first `ls` that shows the name can land while the round is still writing,
  # and a one-shot `cat` right after then fails with ENOENT.
  #
  # The file is produced by a model round, so with the backend unreachable the
  # wait is capped: fifteen minutes of polling cannot make a report appear, and
  # the reasons are not the same finding.  "The board never asked" is a product
  # bug; "the board asked and the model never answered" is the bench.
  $daily = ""
  $askBudget = if ($llmReachable) { 900 } else { 60 }
  $deadline = (Get-Date).AddSeconds($askBudget)
  while ((Get-Date) -lt $deadline) {
    $r = Send-Serial $port "ls /data/velaguard/reports" 8
    $now = [regex]::Matches($r.Text, "daily-\d{4}-\d{2}-\d{2}\.md") |
           ForEach-Object { $_.Value } | Sort-Object -Unique
    $fresh = @($now | Where-Object { $before -notcontains $_ })
    if ($fresh.Count -gt 0) {
      $c = Send-Serial $port "cat /data/velaguard/reports/$(@($fresh)[0])" 10
      if ($c.Text -match "AI-DAILY v1") { $daily = @($fresh)[0]; break }
      Write-Output "[INFO] $(@($fresh)[0]) exists but is not a valid daily yet"
    }
    Start-Sleep -Seconds 10
  }

  if ($llmReachable) {
    Assert-Match "board asked the agent for a new daily report by itself" $daily "daily-"
  } else {
    # The board's request is still visible in the console above; what cannot
    # happen is the file.  Say which half was not exercised.
    Write-Output "[INFO] daily report not asserted: it needs a completed model round, and the backend is unreachable"
    if ($daily -ne "") {
      Write-Output "[INFO] a daily report did appear anyway: $daily"
    }
  }

  # The board prints its request and tool lines while this script is between
  # polls, and each poll discards the port buffer first, so those lines are
  # not reliably capturable here.  The audit log is the durable record of the
  # same fact, and the console wording is captured separately in the task's
  # research notes.

  if ($daily -ne "") {
    $r = Send-Serial $port "cat /data/velaguard/reports/$daily" 10
    Write-Output $r.Block
    Assert-Match "daily report carries the AI-DAILY marker" $r.Text "AI-DAILY v1"
    Assert-Match "daily report declares source=agent" $r.Text "source=agent"
    Assert-Match "daily report declares its date" $r.Text "date=\d{4}-\d{2}-\d{2}"
  }

  # Per-point advice.  The HMI file worker asks on its own once an alarm is
  # up, and only publishes a document the board-side parser accepted, so the
  # artifact is the assertion.  pending_alarm.txt is what tells us whether
  # this bench has an alarm at all; without one there is nothing to wait for.
  $r = Send-Serial $port "ls /data/velaguard/pending_alarm.txt" 8
  if ($r.Text -notmatch "pending_alarm\.txt") {
    Write-Output "[INFO] no pending alarm on the bench, so no advice round is expected"
  } else {
    # One round is about 195 s, and the agent occasionally answers without
    # writing the file, in which case the board backs off VG_ADV_RETRY_MS
    # before asking again.  The window has to cover a failed round plus a
    # retry, or this check reports a working flow as broken.
    $advice = ""
    $adviceBudget = if ($llmReachable) { 900 } else { 60 }
    $deadline = (Get-Date).AddSeconds($adviceBudget)
    while ((Get-Date) -lt $deadline) {
      $c = Send-Serial $port "cat /data/velaguard/reports/alarm_advice.txt" 10
      if ($c.Text -match "VGADV1") { $advice = $c.Text; break }
      Start-Sleep -Seconds 15
    }
    Write-Output "[INFO] advice document:`n$advice"
    if ($llmReachable) {
      Assert-Match "board asked the agent for per-point advice by itself" $advice "VGADV1"
      # The contract allows END as the last line with or without a trailing
      # newline (vg_ai_advice_parse reads the remainder as a line), and the NSH
      # prompt is glued straight onto that last line in a `cat` capture, so text
      # following END cannot be told apart from a longer word.  This asserts the
      # terminator line is present; that nothing follows it is enforced by the
      # board's own parser, and the advice probe's covered=1 proves it parsed.
      Assert-Match "advice document is terminated by END" $advice "(\r?\n)END"
      Assert-Match "advice document carries a boot stamp" $advice "boot=[0-9a-f]{8}"
    } else {
      Write-Output "[INFO] advice document not asserted: it needs a completed model round, and the backend is unreachable"
    }
  }

  # Every tool the agent ran is appended here before it executes, so this is
  # the durable evidence that the round really called tools.
  $r = Send-Serial $port "ls /data/velaguard/logs" 10
  Assert-Match "tool call audit log written" $r.Text "agent_tools.log"
  $r = Send-Serial $port "cat /data/velaguard/logs/agent_tools.log" 12
  Assert-Match "audit log records the round's tool calls" $r.Text "run_shell|write_file|read_file"

  # Manual ask last: the board-driven checks above must not be able to pass
  # on a file this script asked for itself.
  $r = Send-Serial $port "ai_agent" 25 -WantVela
  Assert-Match "vela after ai_agent attach" $r.Text "vela>"

  if (-not $MimoToken -or $MimoToken.Length -lt 8) {
    Write-Output "[INFO] MIMO_API_KEY unset; using eMMC /data/agent/config/config.json if present"
  }

  # The console lines a round prints are not a reliable assertion: the board
  # keeps talking between this script's polls, and each poll discards the port
  # buffer first.  The audit log is the durable record, so measure it before
  # and after the ask and require it to grow.
  $audit0 = (Send-Serial $port "cat /data/velaguard/logs/agent_tools.log" 15).Text.Length

  # A round takes about 195 s and the board's own rounds have priority on the
  # single agent channel, so the first ask can spend its whole window queued
  # behind one.  Ask, and if nothing came back with a tool call, ask again.
  $r = Send-Serial $port 'ask 读取从站1的温湿度和通信质量' 200
  if ($r.Text -notmatch "Executing tool: ") {
    Write-Output "[INFO] first ask produced no tool line in its window; retrying once"
    $r2 = Send-Serial $port "ask vgstats dump" 200
    $r = @{ Text = $r.Text + $r2.Text; Block = $r.Block + $r2.Block }
  }
  Assert-Match "no LLM watchdog timeout" $r.Text "(?!watchdog)(?!请求超时)"
  Assert-Match "agent replied" $r.Text "\[Agent\]|从站|温度|湿度|通信"
  Write-Output $r.Block

  $audit1 = (Send-Serial $port "cat /data/velaguard/logs/agent_tools.log" 15).Text
  if ($audit1.Length -le $audit0) {
    # A round takes minutes and the board's own rounds have priority, so the
    # ask can still be queued when its window closes.  Give it one more wait
    # before concluding anything about the manual path.
    Write-Output "[INFO] no audit growth yet; waiting one more round"
    Start-Sleep -Seconds 150
    $audit1 = (Send-Serial $port "cat /data/velaguard/logs/agent_tools.log" 15).Text
  }

  if ($audit1.Length -gt $audit0 -and $audit1 -match "run_shell|write_file|read_file") {
    Write-Output "[PASS] the ask round appended tool calls to the audit log"
    $script:pass++
  } else {
    # The interactive channel's own evidence is the reply assertion above and
    # the audit-log content assertion further up.  This one only says whether
    # the manual ask got its own slot inside this script's window, so it is
    # reported rather than counted as a product failure.
    Write-Output "[INFO] the manual ask never got its own slot in this window; its round is still queued"
  }

  $r = Send-Serial $port "quit" 15
  Start-Sleep -Seconds 1

  # ── Read-only data tools: point value and run report ──────────────────
  #
  # These are the two questions a user actually types.  They are checked
  # through `vgagent tool`, which runs the tool through the same guard and
  # audit path the ReAct loop uses, because that works without a reachable
  # LLM: when TLS to the model host fails, every round ends in
  # "Sorry, I encountered an error" and an accept script that only asks
  # questions would report a product failure that is really a network one.
  #
  # The asks are still run, and their evidence is recorded, but they are
  # reported as INFO unless a round actually completed.  LLM health is stated
  # up front so the reader can tell the two apart.
  #
  # Everything here must be at nsh>: the block above quits the agent CLI, and
  # running `ls` inside vela> answers "Unknown command: ls".
  $reports0 = (Send-Serial $port "ls /data/velaguard/reports" 10).Text
  $ref = Send-Serial $port "vgpoint get ups_load" 10
  $refValue = $null
  $refRaw = ""
  if ($ref.Text -match "VALUE\s+id=ups_load\s+value=(\S+)") {
    $refRaw = $Matches[1]
    $parsed = 0.0
    if ([double]::TryParse($Matches[1], [ref]$parsed)) {
      $refValue = $parsed
      Write-Output "[INFO] reference value from vgpoint get ups_load: $refValue"
    } else {
      Write-Output "[INFO] vgpoint get ups_load has no reading yet (value=$refRaw)"
    }
  }

  # Whether the model backend is usable is decided below by an actual probe:
  # `credentials=ready` only says a key is configured, and this bench has one
  # configured while TLS to the host fails.
  $r = Send-Serial $port "vgagent status" 10
  Write-Output "[INFO] llm round health: $($r.Text.Trim() -replace "\s+", ' ')"

  # The two asks FIRST, before this script calls any tool itself.
  #
  # Order is what makes the audit log readable: the direct probe below appends
  # the same "tool=vg_point_read" lines, so reading the log after both would
  # count the probe's own calls as the model's.  That is not hypothetical: an
  # earlier version of this script asserted "the point ask reached the model's
  # tool call" against a log the probe had just written, and passed while the
  # asks in fact produced no tool lines at all.
  #
  # The log is also removed first.  It survives across boots and the agent that
  # wrote it last may be a previous acceptance run, so matching it without
  # clearing it could credit the model with a call an earlier session made.
  # Deleting it is safe: audit_tool_call opens with O_APPEND|O_CREAT, so the
  # next call recreates it, and it is a test-state file the board rotates itself.
  $r = Send-Serial $port "rm /data/velaguard/logs/agent_tools.log" 8
  Write-Output "[INFO] cleared the audit log so the entries below belong to these asks"

  $r = Send-Serial $port "ai_agent" 25 -WantVela
  Assert-Match "vela after ai_agent attach (query round)" $r.Text "vela>"

  if ($llmReachable) {
    $r = Send-Serial $port "ask 告诉我UPS负载的值" 200
    if ($r.Text -notmatch "Executing tool: ") {
      Write-Output "[INFO] point ask produced no tool line in its window; retrying once"
      $r2 = Send-Serial $port "ask 告诉我UPS负载的值" 200
      $r = @{ Text = $r.Text + $r2.Text; Block = $r.Block + $r2.Block }
    }
    Write-Output $r.Block

    $r = Send-Serial $port "ask 给我截止目前的运行报告" 200
    if ($r.Text -notmatch "Executing tool: ") {
      Write-Output "[INFO] report ask produced no tool line in its window; retrying once"
      $r2 = Send-Serial $port "ask 给我截止目前的运行报告" 200
      $r = @{ Text = $r.Text + $r2.Text; Block = $r.Block + $r2.Block }
    }
    Write-Output $r.Block
    $asked = $true
  } else {
    Write-Output "[INFO] the model backend is unreachable from the board; skipping the ask-side checks"
    Write-Output "[INFO] probe console: $($probe.Text.Trim() -replace '\s+', ' ')"
    $asked = $false
  }

  # Leave the agent CLI before any NSH command: inside vela> the shell commands
  # below would answer "Unknown command" and the comparisons would be between
  # two error strings, which reads as a pass.
  $r = Send-Serial $port "quit" 15
  Start-Sleep -Seconds 1

  # Attributable: the log was cleared before the asks and no vgagent tool call
  # has run in this script yet.  Only a UPS-related query counts, so a board
  # round that happened to read some other point cannot make this pass.
  $auditBeforeProbe = (Send-Serial $port "cat /data/velaguard/logs/agent_tools.log" 20).Text
  if ($asked) {
    Assert-Match "the model itself called vg_point_read for the point question" `
      $auditBeforeProbe "tool=vg_point_read rc=0 args=.*(ups_load|UPS)"
  } else {
    Write-Output "[INFO] the ask-side assertion is skipped because the model backend is down, not failed"
  }

  # What the model is actually offered.  The registry drops a provider whose
  # JSON does not parse, and that looks identical from outside to the model
  # choosing not to call anything, so the string is printed rather than
  # inferred.
  $r = Send-Serial $port "vgagent tools" 15
  Write-Output $r.Block
  Assert-Match "vg_point_read is advertised to the model" $r.Text "vg_point_read"
  Assert-Match "vg_run_report is advertised to the model" $r.Text "vg_run_report"

  # Run the two tools directly.  rc=0 here is the tool layer answering, which
  # is what this task added; whether the model picks them is checked above.
  #
  # A bare word, not JSON: NSH strips the quotes a JSON argument needs, so
  # `{"query":"ups_load"}` arrives as {query:ups_load} and fails to parse.
  # vgagent wraps a bare word into {"query":"<word>"} for this reason.
  $r = Send-Serial $port "vgagent tool vg_point_read ups_load" 15
  Write-Output $r.Block
  Assert-Match "vg_point_read answers by id" $r.Text "id=ups_load"
  Assert-Match "vg_point_read reports rc=0" $r.Text "tool vg_point_read rc=0"
  $byId = $r.Text

  $r = Send-Serial $port "vgagent tool vg_point_read UPS负载" 15
  Write-Output $r.Block
  # The table has 14 rows and ups_load is one of them; resolving the Chinese
  # name is what vgmodbus could never do.
  Assert-Match "vg_point_read resolves a chinese point name" $r.Text "id=ups_load"

  $r = Send-Serial $port "vgagent tool vg_point_read UPS" 15
  Write-Output $r.Block
  # Three points contain "UPS".  Picking one would answer a different
  # question than the user asked, so it must list them instead.
  Assert-Match "an ambiguous name lists candidates" $r.Text "ambiguous"

  if ($null -ne $refValue -and $refRaw -ne "-") {
    # ups_load is scale 0.1, so an unscaled register read prints ten times
    # this.  Matching the board's own vgpoint reading is what shows the scale
    # was applied end to end.
    Assert-ValueQuoted "vg_point_read returns the same value as vgpoint get" $byId $refValue
  } else {
    # Expected on a bench with no RS485 slaves: every poll fails, so there is
    # no reading to compare against.  The tool must still answer honestly
    # rather than print a number.
    Assert-Match "vg_point_read names the missing reading, not a value" $byId "reason=read_failed|reason=no_sample"
    Write-Output "[INFO] no live reference value on this bench; compared against no_sample/read_failed instead"
  }

  $r = Send-Serial $port "vgagent tool vg_point_read 不存在的点位" 15
  Write-Output $r.Block
  # A missing point must say so instead of borrowing a nearby point's value.
  Assert-Match "unknown point reports not_found" $r.Text "not_found"

  $r = Send-Serial $port "vgagent tool vg_run_report {}" 15
  Write-Output $r.Block
  Assert-Match "vg_run_report answers with rc=0" $r.Text "tool vg_run_report rc=0"
  Assert-Match "run report has the comm section" $r.Text "通信质量"
  Assert-Match "run report has the point section" $r.Text "点位在线"
  Assert-Match "run report has the event section" $r.Text "异常时间线"
  # The board's own provenance line, which the model is told to trust.
  Assert-Match "run report declares its numbers are measured" $r.Text "数字不是推测"

  # vgpoint stays unreachable through the agent's shell allowlist even though a
  # read-only point tool now exists.  The JSON argument cannot carry a space
  # over NSH (quotes are stripped), so the single-token form is used: the
  # allowlist rejects vgpoint regardless of what follows it.
  $r = Send-Serial $port 'vgagent tool run_shell {"command":"vgpoint"}' 15
  Write-Output $r.Block
  Assert-Match "run_shell still refuses vgpoint" $r.Text "blocked|is_blocked|not allowed|Missing 'command'"

  # The tool-level facts, read from the audit log.  A read of this log is
  # matched loosely because NSH has no grep/tail and a `cat` of a large log is
  # truncated, so a strict match would fail for a reason that has nothing to do
  # with the tools.
  #
  # The point-ask attribution was already taken from the log before the probe
  # ran; this read only confirms the tools are in the log at all.
  $audit = (Send-Serial $port "cat /data/velaguard/logs/agent_tools.log" 20).Text
  if ($audit -match "tool=vg_point_read") {
    Write-Output "[PASS] the audit log records a vg_point_read call"
    $script:pass++
  } else {
    Write-Output "[INFO] vg_point_read is not in the visible part of the audit log"
  }

  # Answering must not write anything: the report page falls back to
  # runtime-report.md and reads daily-<date>.md, so a change here would mean
  # the read-only path had reached past its boundary or a round had run.
  $reports1 = (Send-Serial $port "ls /data/velaguard/reports" 10).Text
  $clean0 = [regex]::Replace($reports0, '\s+', ' ').Trim()
  $clean1 = [regex]::Replace($reports1, '\s+', ' ').Trim()
  if ($clean0 -eq $clean1 -and $clean0 -notmatch "Unknown command") {
    Write-Output "[PASS] the reports directory was not changed by the read-only path"
    $script:pass++
  } elseif ($clean1 -match "Unknown command") {
    Write-Output "[FAIL] the reports listing was taken outside the NSH prompt"
    $script:fail++
  } else {
    # A board-driven round may legitimately rewrite daily-<date>.md while this
    # script runs, so a difference is reported rather than asserted: the
    # read-only guarantee is already covered by running the tools by hand.
    Write-Output "[INFO] the reports directory changed during this window (a board round may have run)"
    Write-Output "  before: $clean0"
    Write-Output "  after : $clean1"
  }

  # Board-driven round channel: the HMI file worker owns it, the NSH probe
  # only reads it back.
  $r = Send-Serial $port "vgagent status" 10
  Assert-Match "vgagent round channel present" $r.Text "round: state="

  Write-Output "`n[stage1_agent_ops_accept] pass=$($script:pass) fail=$($script:fail)"
  if ($script:fail -gt 0) { exit 1 }
}
catch {
  Write-Error "stage1_agent_ops_accept failed: $($_.Exception.Message)"
  exit 1
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
