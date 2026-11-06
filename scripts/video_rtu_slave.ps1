# VelaGuard video bench: scripted Modbus RTU slave for the 80-second shot list.
#
# The board is the only Modbus master. This process only answers FC03/FC04
# reads for the slave addresses listed in the scenario file. It never starts
# bus traffic and never touches /data on the board.
#
# Typical use on the shooting PC:
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\video_rtu_slave.ps1
#   .\scripts\video_rtu_slave.ps1 -Port COM11
#   .\scripts\video_rtu_slave.ps1 -Port COM11 -Auto -AutoDelaySec 5
#   .\scripts\video_rtu_slave.ps1 -ListPorts
#   .\scripts\video_rtu_slave.ps1 -SelfTest
#
# Keys while running: 1..N = phase, 0 = last phase, r = first phase,
# m = mark the log (for edit sync), q = quit.
# The same commands can be written to the control file, one at a time:
#   echo rise > .debug\video-rtu-slave-control.txt
#
# Log: .debug/video-rtu-slave.log (PHASE / MARK / STATUS / STOP lines).
param(
  [string]$Port = "",
  [int]$Baud = 0,
  [string]$Scenario = "",
  [string]$ControlPath = "",
  [string]$LogPath = "",
  [switch]$Auto,
  [int]$AutoDelaySec = 5,
  [ValidateRange(0, 1000)][int]$ReplyDelayMs = 0,
  [switch]$EnableRts,
  [switch]$EnableDtr,
  [ValidateRange(1, 168)][int]$Hours = 8,
  [int]$Seed = 20261106,
  [switch]$SelfTest,
  [switch]$ListPorts
)

$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$Inv = [System.Globalization.CultureInfo]::InvariantCulture
$repoRoot = Split-Path -Parent $PSScriptRoot

if ($Scenario -eq "") {
  $Scenario = Join-Path $repoRoot "config\modbus-slave\video_scene_80s.json"
}
if ($ControlPath -eq "") {
  $ControlPath = Join-Path $repoRoot ".debug\video-rtu-slave-control.txt"
}
if ($LogPath -eq "") {
  $LogPath = Join-Path $repoRoot ".debug\video-rtu-slave.log"
}
$Scenario = [IO.Path]::GetFullPath($Scenario)
$ControlPath = [IO.Path]::GetFullPath($ControlPath)
$LogPath = [IO.Path]::GetFullPath($LogPath)

function Show-ComPorts {
  $listed = @()
  try {
    $listed = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
      Where-Object { $_.Name -match '\(COM\d+\)' } |
      Sort-Object Name)
  } catch { }
  if ($listed.Count -eq 0) {
    $names = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($names.Count -eq 0) {
      Write-Host "No COM port found."
    } else {
      foreach ($n in $names) { Write-Host ("  {0}" -f $n) }
    }
    return
  }
  foreach ($d in $listed) { Write-Host ("  {0}" -f $d.Name) }
}

if ($ListPorts) {
  Write-Host "COM ports on this PC:"
  Show-ComPorts
  exit 0
}

if (-not (Test-Path -LiteralPath $Scenario)) {
  throw "Scenario file not found: $Scenario"
}

if ($Port -eq "" -and -not $SelfTest) {
  Write-Host "COM ports on this PC:"
  Show-ComPorts
  $answer = Read-Host "RS485 adapter port (for example COM11)"
  if ($null -ne $answer) { $Port = $answer.Trim() }
  if ($Port -eq "") { throw "No port given. Use -Port COMxx." }
}

function Fmt-Num([double]$v) {
  return $v.ToString("0.######", $Inv)
}

function Clean-Text([string]$s) {
  if ($null -eq $s) { return "" }
  return ($s -replace '[|\r\n]', ' ')
}

$scenarioJson = Get-Content -Raw -Encoding UTF8 -LiteralPath $Scenario | ConvertFrom-Json
if ($null -eq $scenarioJson.points -or @($scenarioJson.points).Count -lt 1) {
  throw "Scenario has no points: $Scenario"
}
if ($null -eq $scenarioJson.phases -or @($scenarioJson.phases).Count -lt 1) {
  throw "Scenario has no phases: $Scenario"
}

$busBaud = if ($Baud -gt 0) { $Baud } elseif ($null -ne $scenarioJson.baud) { [int]$scenarioJson.baud } else { 9600 }
$lines = New-Object System.Collections.Generic.List[string]
[void]$lines.Add("V|$(Clean-Text ([string]$scenarioJson.name))|$busBaud")

foreach ($pt in @($scenarioJson.points)) {
  $dtype = if ($null -ne $pt.dtype -and "$($pt.dtype)" -ne "") { [string]$pt.dtype } else { "int16" }
  $jitter = if ($null -ne $pt.jitter) { [double]$pt.jitter } else { 0.0 }
  $step = if ($null -ne $pt.step_per_s) { [double]$pt.step_per_s } else { 0.0 }
  $scale = if ($null -ne $pt.scale) { [double]$pt.scale } else { 1.0 }
  [void]$lines.Add(("P|{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}" -f
    (Clean-Text ([string]$pt.id)),
    (Clean-Text ([string]$pt.name)),
    [int]$pt.addr, [int]$pt.reg, $dtype,
    (Fmt-Num $scale), (Fmt-Num ([double]$pt.start)),
    (Fmt-Num $jitter), (Fmt-Num $step)))
}

function Format-Pairs($obj) {
  if ($null -eq $obj) { return "" }
  $items = @()
  foreach ($prop in $obj.PSObject.Properties) {
    $items += ("{0}={1}" -f $prop.Name, (Fmt-Num ([double]$prop.Value)))
  }
  return ($items -join ';')
}

foreach ($ph in @($scenarioJson.phases)) {
  $ramp = if ($null -ne $ph.ramp_sec) { [double]$ph.ramp_sec } else { 0.0 }
  [void]$lines.Add(("F|{0}|{1}|{2}|{3}|{4}" -f
    (Clean-Text ([string]$ph.id)),
    (Clean-Text ([string]$ph.label)),
    (Fmt-Num $ramp),
    (Format-Pairs $ph.values),
    (Format-Pairs $ph.jitter)))
}

if ($null -ne $scenarioJson.auto) {
  foreach ($a in @($scenarioJson.auto)) {
    [void]$lines.Add(("A|{0}|{1}" -f (Clean-Text ([string]$a.phase)), (Fmt-Num ([double]$a.sec))))
  }
}

$cfgText = ($lines -join "`n") + "`n"

$csharp = @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Diagnostics;
using System.Text;
using System.Threading;

public static class VgVideoSlave
{
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    sealed class Point
    {
        public string Id = "";
        public string Name = "";
        public int Addr;
        public int Reg;
        public string Dtype = "int16";
        public double Scale = 1.0;
        public double Start;
        public double Jitter;
        public double StepPerSec;
        public double Center;
        public double Target;
        public double RampFrom;
        public double RampSec;
        public long RampStartMs;
        public double Drift;
        public long LastMs;
    }

    sealed class Phase
    {
        public string Id = "";
        public string Label = "";
        public double RampSec;
        public Dictionary<string, double> Values = new Dictionary<string, double>();
        public Dictionary<string, double> Jitters = new Dictionary<string, double>();
    }

    sealed class AutoStep
    {
        public string Phase = "";
        public double Sec;
    }

    sealed class Config
    {
        public string Name = "";
        public int Baud = 9600;
        public List<Point> Points = new List<Point>();
        public List<Phase> Phases = new List<Phase>();
        public List<AutoStep> Auto = new List<AutoStep>();
        public Dictionary<string, Point> ById = new Dictionary<string, Point>();
        public Dictionary<int, Dictionary<int, Point>> ByAddr = new Dictionary<int, Dictionary<int, Point>>();
        public List<int> Slaves = new List<int>();
    }

    static double Num(string s)
    {
        return double.Parse(s, Inv);
    }

    static Config Parse(string text)
    {
        Config cfg = new Config();
        string[] lines = text.Replace("\r", "").Split('\n');
        foreach (string raw in lines)
        {
            if (raw.Length == 0) continue;
            string[] f = raw.Split('|');
            if (f[0] == "V")
            {
                cfg.Name = f.Length > 1 ? f[1] : "";
                if (f.Length > 2) cfg.Baud = int.Parse(f[2], Inv);
            }
            else if (f[0] == "P")
            {
                Point p = new Point();
                p.Id = f[1];
                p.Name = f[2];
                p.Addr = int.Parse(f[3], Inv);
                p.Reg = int.Parse(f[4], Inv);
                p.Dtype = f[5];
                p.Scale = Num(f[6]);
                p.Start = Num(f[7]);
                p.Jitter = Num(f[8]);
                p.StepPerSec = Num(f[9]);
                p.Center = p.Start;
                p.Target = p.Start;
                cfg.Points.Add(p);
                cfg.ById[p.Id] = p;
                if (!cfg.ByAddr.ContainsKey(p.Addr)) cfg.ByAddr[p.Addr] = new Dictionary<int, Point>();
                cfg.ByAddr[p.Addr][p.Reg] = p;
                if (!cfg.Slaves.Contains(p.Addr)) cfg.Slaves.Add(p.Addr);
            }
            else if (f[0] == "F")
            {
                Phase ph = new Phase();
                ph.Id = f[1];
                ph.Label = f[2];
                ph.RampSec = Num(f[3]);
                if (f.Length > 4 && f[4].Length > 0)
                {
                    foreach (string pair in f[4].Split(';'))
                    {
                        string[] kv = pair.Split('=');
                        ph.Values[kv[0]] = Num(kv[1]);
                    }
                }
                if (f.Length > 5 && f[5].Length > 0)
                {
                    foreach (string pair in f[5].Split(';'))
                    {
                        string[] kv = pair.Split('=');
                        ph.Jitters[kv[0]] = Num(kv[1]);
                    }
                }
                cfg.Phases.Add(ph);
            }
            else if (f[0] == "A")
            {
                AutoStep a = new AutoStep();
                a.Phase = f[1];
                a.Sec = Num(f[2]);
                cfg.Auto.Add(a);
            }
        }
        return cfg;
    }

    static ushort Crc(byte[] a, int n)
    {
        ushort c = 0xffff;
        for (int i = 0; i < n; i++)
        {
            c ^= a[i];
            for (int j = 0; j < 8; j++)
            {
                c = (ushort)(((c & 1) != 0) ? (c >> 1) ^ 0xa001 : c >> 1);
            }
        }
        return c;
    }

    static byte[] Frame(int slave, int fc, int reg, int qty)
    {
        byte[] f = new byte[8];
        f[0] = (byte)slave;
        f[1] = (byte)fc;
        f[2] = (byte)(reg >> 8);
        f[3] = (byte)reg;
        f[4] = (byte)(qty >> 8);
        f[5] = (byte)qty;
        ushort c = Crc(f, 6);
        f[6] = (byte)c;
        f[7] = (byte)(c >> 8);
        return f;
    }

    static byte[] Exception(int slave, int fc, int code)
    {
        byte[] r = new byte[5];
        r[0] = (byte)slave;
        r[1] = (byte)(fc | 0x80);
        r[2] = (byte)code;
        ushort c = Crc(r, 3);
        r[3] = (byte)c;
        r[4] = (byte)(c >> 8);
        return r;
    }

    static ushort Encode(Point p, double v)
    {
        double scale = Math.Abs(p.Scale) < 1e-9 ? 1.0 : p.Scale;
        double r = Math.Round(v / scale, MidpointRounding.AwayFromZero);
        if (p.Dtype == "uint16")
        {
            if (r < 0) r = 0;
            if (r > 65535) r = 65535;
            return (ushort)r;
        }
        if (r < short.MinValue) r = short.MinValue;
        if (r > short.MaxValue) r = short.MaxValue;
        return (ushort)(short)r;
    }

    static void RefreshRamp(Point p, long nowMs)
    {
        if (p.RampSec <= 0) return;
        double t = (nowMs - p.RampStartMs) / (p.RampSec * 1000.0);
        if (t >= 1.0)
        {
            p.Center = p.Target;
            p.RampSec = 0;
            return;
        }
        if (t < 0) t = 0;
        p.Center = p.RampFrom + (p.Target - p.RampFrom) * t;
    }

    static ushort ReadRaw(Point p, long nowMs, Random rng)
    {
        RefreshRamp(p, nowMs);
        double dt = 0;
        if (p.LastMs > 0 && nowMs > p.LastMs) dt = (nowMs - p.LastMs) / 1000.0;
        p.LastMs = nowMs;
        if (p.StepPerSec != 0 && dt > 0) p.Center += p.StepPerSec * dt;
        if (p.Jitter > 0)
        {
            double step = p.Jitter * 0.3 * (rng.NextDouble() * 2.0 - 1.0);
            p.Drift = p.Drift * 0.995 + step;
            if (p.Drift > p.Jitter) p.Drift = p.Jitter;
            if (p.Drift < -p.Jitter) p.Drift = -p.Jitter;
        }
        return Encode(p, p.Center + p.Drift);
    }

    static void ApplyPhase(Config cfg, Phase ph, long nowMs)
    {
        foreach (KeyValuePair<string, double> kv in ph.Values)
        {
            Point p;
            if (!cfg.ById.TryGetValue(kv.Key, out p)) continue;
            RefreshRamp(p, nowMs);
            double cur = p.Center + p.Drift;
            p.Target = kv.Value;
            if (ph.RampSec > 0 && Math.Abs(cur - kv.Value) > 1e-9)
            {
                p.RampFrom = cur;
                p.RampSec = ph.RampSec;
                p.RampStartMs = nowMs;
            }
            else
            {
                p.Center = kv.Value;
                p.RampSec = 0;
                p.Drift = 0;
            }
        }
        foreach (KeyValuePair<string, double> kv in ph.Jitters)
        {
            Point p;
            if (!cfg.ById.TryGetValue(kv.Key, out p)) continue;
            p.Jitter = kv.Value;
            if (kv.Value <= 0) p.Drift = 0;
        }
    }

    static byte[] Handle(Config cfg, Random rng, long nowMs, byte[] q, out bool addressed)
    {
        addressed = false;
        if (q == null || q.Length != 8) return null;
        int slave = q[0];
        if (!cfg.Slaves.Contains(slave)) return null;
        addressed = true;
        int fc = q[1];
        int reg = (q[2] << 8) | q[3];
        int qty = (q[4] << 8) | q[5];
        if (fc != 3 && fc != 4) return Exception(slave, fc, 1);
        if (qty < 1 || qty > 125) return Exception(slave, fc, 3);
        Dictionary<int, Point> regs;
        if (!cfg.ByAddr.TryGetValue(slave, out regs)) return Exception(slave, fc, 2);
        ushort[] vals = new ushort[qty];
        for (int i = 0; i < qty; i++)
        {
            Point p;
            if (!regs.TryGetValue(reg + i, out p)) return Exception(slave, fc, 2);
            vals[i] = ReadRaw(p, nowMs, rng);
        }
        byte[] r = new byte[5 + qty * 2];
        r[0] = (byte)slave;
        r[1] = (byte)fc;
        r[2] = (byte)(qty * 2);
        for (int i = 0; i < qty; i++)
        {
            r[3 + i * 2] = (byte)(vals[i] >> 8);
            r[4 + i * 2] = (byte)(vals[i] & 0xff);
        }
        ushort c = Crc(r, r.Length - 2);
        r[r.Length - 2] = (byte)c;
        r[r.Length - 1] = (byte)(c >> 8);
        return r;
    }

    sealed class Session
    {
        public Config Cfg;
        public Random Rng;
        public Stopwatch Sw = new Stopwatch();
        public StreamWriter Log;
        public SerialPort Sp;
        public string ControlPath = "";
        public int ReplyDelayMs;
        public int Hours = 8;
        public bool Keys;
        public bool Stop;
        public long Requests;
        public long Replies;
        public long Other;
        public long BadCrc;
        public List<byte> Rx = new List<byte>(512);
        public string PhaseId = "-";
        public bool AutoMode;
        public long AutoEarliest;
        public long AutoStart = -1;
        public long AutoCum;
        public long AutoTotal;
        public int AutoIdx;
        public long NextControl;
        public long NextStatus;
        public long NextDash;
        public string LastControl = "";
        public int RawBudget = 512;

        public void LogLine(string s)
        {
            if (Log != null) Log.WriteLine(s);
        }

        public string Fmt(string id)
        {
            Point p;
            if (!Cfg.ById.TryGetValue(id, out p)) return "-";
            RefreshRamp(p, Sw.ElapsedMilliseconds);
            return (p.Center + p.Drift).ToString("F1", Inv);
        }

        public void Apply(Phase ph)
        {
            ApplyPhase(Cfg, ph, Sw.ElapsedMilliseconds);
            PhaseId = ph.Id;
            string line = string.Format(Inv, "[{0,6:F1}s] phase={1}  {2}",
                Sw.Elapsed.TotalSeconds, ph.Id, ph.Label);
            if (!Console.IsOutputRedirected) Console.WriteLine();
            Console.WriteLine(line);
            LogLine(string.Format(Inv, "PHASE {0:o} elapsed_s={1:F1} id={2} label={3}",
                DateTime.Now, Sw.Elapsed.TotalSeconds, ph.Id, ph.Label));
        }

        public void ApplyByIndex(int idx)
        {
            if (idx < 0 || idx >= Cfg.Phases.Count) return;
            Apply(Cfg.Phases[idx]);
        }

        public void ApplyById(string id)
        {
            for (int i = 0; i < Cfg.Phases.Count; i++)
            {
                if (string.Equals(Cfg.Phases[i].Id, id, StringComparison.OrdinalIgnoreCase))
                {
                    ApplyByIndex(i);
                    return;
                }
            }
        }

        public void Mark(string label)
        {
            LogLine(string.Format(Inv, "MARK {0:o} elapsed_s={1:F1} phase={2} label={3}",
                DateTime.Now, Sw.Elapsed.TotalSeconds, PhaseId, label));
            if (!Console.IsOutputRedirected) Console.WriteLine();
            Console.WriteLine(string.Format(Inv, "[{0,6:F1}s] MARK {1}", Sw.Elapsed.TotalSeconds, label));
        }

        void PollKeys()
        {
            try
            {
                while (Console.KeyAvailable)
                {
                    ConsoleKeyInfo k = Console.ReadKey(true);
                    char ch = char.ToLowerInvariant(k.KeyChar);
                    if (ch == 'q') { Stop = true; return; }
                    if (ch == 'm') { Mark("key"); continue; }
                    if (ch == 'r') { AutoMode = false; ApplyByIndex(0); continue; }
                    if (ch == '0') { AutoMode = false; ApplyByIndex(Cfg.Phases.Count - 1); continue; }
                    if (ch >= '1' && ch <= '9')
                    {
                        int idx = (int)(ch - '1');
                        if (idx < Cfg.Phases.Count) { AutoMode = false; ApplyByIndex(idx); }
                    }
                }
            }
            catch
            {
                Keys = false;
                Console.WriteLine("keyboard disabled; use the control file");
            }
        }

        void PollControl()
        {
            try
            {
                if (ControlPath.Length == 0 || !File.Exists(ControlPath)) return;
                string text = File.ReadAllText(ControlPath).Trim();
                if (text.Length == 0 || text == LastControl) return;
                LastControl = text;
                string low = text.ToLowerInvariant();
                if (low == "stop" || low == "quit") { Stop = true; return; }
                if (low == "mark") { Mark("control"); return; }
                if (low == "r") { AutoMode = false; ApplyByIndex(0); return; }
                int num;
                if (int.TryParse(low, NumberStyles.Integer, Inv, out num))
                {
                    AutoMode = false;
                    if (num == 0) ApplyByIndex(Cfg.Phases.Count - 1);
                    else ApplyByIndex(num - 1);
                    return;
                }
                AutoMode = false;
                ApplyById(text);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        void PollAuto()
        {
            long now = Sw.ElapsedMilliseconds;
            if (AutoStart < 0)
            {
                if (now < AutoEarliest) return;
                AutoStart = now;
                AutoCum = 0;
                AutoIdx = 0;
                AutoTotal = 0;
                foreach (AutoStep step in Cfg.Auto) AutoTotal += (long)(step.Sec * 1000.0);
                if (Cfg.Auto.Count > 0)
                {
                    ApplyById(Cfg.Auto[0].Phase);
                    AutoCum = (long)(Cfg.Auto[0].Sec * 1000.0);
                    AutoIdx = 1;
                }
                Console.WriteLine("AUTO timeline started");
                LogLine("AUTO started");
                return;
            }
            while (AutoIdx < Cfg.Auto.Count && now - AutoStart >= AutoCum)
            {
                ApplyById(Cfg.Auto[AutoIdx].Phase);
                AutoCum += (long)(Cfg.Auto[AutoIdx].Sec * 1000.0);
                AutoIdx++;
            }
            if (now - AutoStart >= AutoTotal)
            {
                AutoMode = false;
                Console.WriteLine("AUTO timeline complete; still serving, keys are live");
                LogLine("AUTO complete");
            }
        }

        int PumpSerial()
        {
            try
            {
                int avail = Sp.BytesToRead;
                if (avail > 0)
                {
                    byte[] chunk = new byte[avail];
                    int n = Sp.Read(chunk, 0, avail);
                    if (n > 0)
                    {
                        int sample = Math.Min(RawBudget, n);
                        if (sample > 0)
                        {
                            LogLine("RX " + BitConverter.ToString(chunk, 0, sample));
                            RawBudget -= sample;
                        }
                        for (int i = 0; i < n; i++) Rx.Add(chunk[i]);
                    }
                }
                while (Rx.Count >= 8)
                {
                    byte[] q = new byte[8];
                    Rx.CopyTo(0, q, 0, 8);
                    ushort c = Crc(q, 6);
                    if (q[6] != (byte)c || q[7] != (byte)(c >> 8))
                    {
                        Rx.RemoveAt(0);
                        BadCrc++;
                        continue;
                    }
                    Rx.RemoveRange(0, 8);
                    bool addressed;
                    byte[] resp = Handle(Cfg, Rng, Sw.ElapsedMilliseconds, q, out addressed);
                    if (!addressed) { Other++; continue; }
                    Requests++;
                    if (resp == null) continue;
                    if (ReplyDelayMs > 0) Thread.Sleep(ReplyDelayMs);
                    Sp.Write(resp, 0, resp.Length);
                    Replies++;
                }
            }
            catch (TimeoutException) { }
            catch (Exception ex)
            {
                LogLine("ERROR serial: " + ex.Message);
                Console.WriteLine();
                Console.WriteLine("serial error: " + ex.Message);
                return 4;
            }
            return 0;
        }

        void Status()
        {
            LogLine(string.Format(Inv, "STATUS {0:o} elapsed_s={1:F1} phase={2} req={3} rep={4} other={5} badcrc={6}",
                DateTime.Now, Sw.Elapsed.TotalSeconds, PhaseId, Requests, Replies, Other, BadCrc));
        }

        void Dash()
        {
            long now = Sw.ElapsedMilliseconds;
            string prefix = "";
            if (AutoMode && AutoStart < 0)
            {
                int remain = (int)Math.Ceiling((AutoEarliest - now) / 1000.0);
                if (remain < 0) remain = 0;
                prefix = string.Format(Inv, "auto in {0}s  ", remain);
            }
            string line = string.Format(Inv,
                "[{0,7:F1}s] {1}{2,-9} acu_ret={3}  ups_load={4}  humid={5}  req={6}  bad={7}",
                Sw.Elapsed.TotalSeconds, prefix, PhaseId,
                Fmt("acu_ret"), Fmt("ups_load"), Fmt("humid"), Requests, BadCrc);
            if (Console.IsOutputRedirected) Console.WriteLine(line);
            else Console.Write("\r" + line + "        ");
        }

        public int Loop()
        {
            while (!Stop)
            {
                if (Sw.Elapsed.TotalHours >= Hours) { LogLine("TIMEOUT hours=" + Hours); break; }
                if (Keys) PollKeys();
                if (Sw.ElapsedMilliseconds >= NextControl)
                {
                    PollControl();
                    NextControl = Sw.ElapsedMilliseconds + 250;
                }
                if (AutoMode) PollAuto();
                int rc = PumpSerial();
                if (rc != 0) return rc;
                if (Sw.ElapsedMilliseconds >= NextStatus)
                {
                    Status();
                    NextStatus = Sw.ElapsedMilliseconds + 10000;
                }
                if (Sw.ElapsedMilliseconds >= NextDash)
                {
                    Dash();
                    NextDash = Sw.ElapsedMilliseconds + (Console.IsOutputRedirected ? 5000 : 500);
                }
                Thread.Sleep(2);
            }
            return 0;
        }
    }

    public static int Run(string cfgText, string port, string controlPath, string logPath,
                          int hours, bool rts, bool dtr, int replyDelayMs, int seed,
                          bool autoMode, int autoDelaySec)
    {
        Config cfg;
        try { cfg = Parse(cfgText); }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR config: " + ex.Message);
            return 2;
        }

        Session s = new Session();
        s.Cfg = cfg;
        s.Rng = new Random(seed);
        s.ReplyDelayMs = replyDelayMs;
        s.Hours = hours;
        s.ControlPath = controlPath;
        try { s.Keys = !Console.IsInputRedirected; }
        catch { s.Keys = false; }

        string fullLog = Path.GetFullPath(logPath);
        string dir = Path.GetDirectoryName(fullLog);
        if (dir != null && dir.Length > 0) Directory.CreateDirectory(dir);

        try
        {
            s.Log = new StreamWriter(fullLog, true, new UTF8Encoding(false));
            s.Log.AutoFlush = true;
            s.Sp = new SerialPort(port, cfg.Baud, Parity.None, 8, StopBits.One);
            s.Sp.ReadTimeout = 100;
            s.Sp.WriteTimeout = 500;
            s.Sp.RtsEnable = rts;
            s.Sp.DtrEnable = dtr;
            s.Sp.Open();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR open " + port + ": " + ex.Message);
            if (s.Log != null)
            {
                s.Log.WriteLine("ERROR open " + port + ": " + ex.Message);
                s.Log.Close();
            }
            return 3;
        }

        s.Sw.Start();
        s.NextControl = 0;
        s.NextStatus = 0;
        s.NextDash = 0;
        s.LogLine(string.Format(Inv,
            "START {0:o} port={1} baud={2} scenario={3} seed={4} reply_delay_ms={5} auto={6}",
            DateTime.Now, port, cfg.Baud, cfg.Name, seed, replyDelayMs, autoMode));

        Console.WriteLine("VelaGuard video RTU slave");
        Console.WriteLine(string.Format(Inv, "  port={0} {1} 8N1  scenario={2}  seed={3}",
            port, cfg.Baud, cfg.Name, seed));
        Console.WriteLine("  phases:");
        for (int i = 0; i < cfg.Phases.Count; i++)
        {
            Console.WriteLine(string.Format(Inv, "    {0}) {1}  {2}",
                i + 1, cfg.Phases[i].Id, cfg.Phases[i].Label));
        }
        Console.WriteLine("  keys: 1..N = phase, 0 = last, r = first, m = mark, q = quit");
        if (!s.Keys) Console.WriteLine("  keyboard disabled (stdin redirected); control file: " + controlPath);
        if (autoMode)
        {
            s.AutoMode = true;
            s.AutoEarliest = s.Sw.ElapsedMilliseconds + autoDelaySec * 1000L;
            Console.WriteLine(string.Format(Inv, "  auto timeline starts in {0}s; any phase key cancels it",
                autoDelaySec));
        }
        else
        {
            s.ApplyByIndex(0);
        }

        // Ignore commands left in the control file by an earlier run.
        try
        {
            if (controlPath.Length > 0 && File.Exists(controlPath))
            {
                s.LastControl = File.ReadAllText(controlPath).Trim();
            }
        }
        catch (IOException) { }

        int rc = s.Loop();

        s.LogLine(string.Format(Inv, "STOP {0:o} req={1} rep={2} other={3} badcrc={4}",
            DateTime.Now, s.Requests, s.Replies, s.Other, s.BadCrc));
        Console.WriteLine();
        Console.WriteLine(string.Format(Inv, "stopped: req={0} rep={1} other={2} badcrc={3}",
            s.Requests, s.Replies, s.Other, s.BadCrc));
        try { s.Sp.Close(); } catch { }
        try { s.Log.Close(); } catch { }
        return rc;
    }

    static Phase FindPhase(Config cfg, string id)
    {
        foreach (Phase ph in cfg.Phases)
        {
            if (string.Equals(ph.Id, id, StringComparison.OrdinalIgnoreCase)) return ph;
        }
        return null;
    }

    static int Check(string name, bool ok, string detail)
    {
        if (ok) Console.WriteLine("[PASS] " + name + (detail.Length > 0 ? "  " + detail : ""));
        else Console.WriteLine("[FAIL] " + name + (detail.Length > 0 ? "  " + detail : ""));
        return ok ? 1 : 0;
    }

    public static int SelfTest(string cfgText)
    {
        Config cfg = Parse(cfgText);
        Random rng = new Random(7);
        int pass = 0;
        int fail = 0;
        Console.WriteLine(string.Format(Inv, "selftest: scenario={0} points={1} phases={2}",
            cfg.Name, cfg.Points.Count, cfg.Phases.Count));

        int r = Check("crc of 01 03 0000 0001", Frame(1, 3, 0, 1)[6] == 0x84 && Frame(1, 3, 0, 1)[7] == 0x0A,
            BitConverter.ToString(Frame(1, 3, 0, 1)));
        if (r == 1) pass++; else fail++;

        foreach (Point p in cfg.Points)
        {
            bool addressed;
            byte[] q = Frame(p.Addr, 3, p.Reg, 1);
            byte[] resp = Handle(cfg, rng, 1, q, out addressed);
            ushort want = Encode(p, p.Start);
            int got = -1;
            bool ok = false;
            double scale = Math.Abs(p.Scale) < 1e-9 ? 1.0 : Math.Abs(p.Scale);
            int tol = (int)Math.Ceiling(p.Jitter / scale) + 1;
            if (addressed && resp != null && resp.Length == 7 && resp[1] == 3 && resp[2] == 2)
            {
                got = (resp[3] << 8) | resp[4];
                ok = Math.Abs(got - want) <= tol;
            }
            r = Check("point " + p.Id, ok,
                string.Format(Inv, "addr={0} reg={1} want={2} got={3} tol={4}",
                    p.Addr, p.Reg, want, got, tol));
            if (r == 1) pass++; else fail++;
        }

        int ghost = 1;
        while (cfg.Slaves.Contains(ghost)) ghost++;
        bool ga;
        byte[] gr = Handle(cfg, rng, 1, Frame(ghost, 3, 0, 1), out ga);
        r = Check("unlisted slave is silent", !ga && gr == null, "addr=" + ghost);
        if (r == 1) pass++; else fail++;

        Point p0 = cfg.Points[0];
        bool ua;
        byte[] ur = Handle(cfg, rng, 1, Frame(p0.Addr, 3, 60000, 1), out ua);
        r = Check("unknown register -> exception 02",
            ua && ur != null && ur[1] == 0x83 && ur[2] == 2,
            ur == null ? "null" : BitConverter.ToString(ur));
        if (r == 1) pass++; else fail++;

        bool fa;
        byte[] fr = Handle(cfg, rng, 1, Frame(p0.Addr, 6, 0, 1), out fa);
        r = Check("fc 06 -> exception 01",
            fa && fr != null && fr[1] == 0x86 && fr[2] == 1,
            fr == null ? "null" : BitConverter.ToString(fr));
        if (r == 1) pass++; else fail++;

        bool qa;
        byte[] qr = Handle(cfg, rng, 1, Frame(p0.Addr, 3, 0, 0), out qa);
        r = Check("qty 0 -> exception 03",
            qa && qr != null && qr[1] == 0x83 && qr[2] == 3,
            qr == null ? "null" : BitConverter.ToString(qr));
        if (r == 1) pass++; else fail++;

        Point acu;
        if (cfg.ById.TryGetValue("acu_ret", out acu))
        {
            Phase rise = FindPhase(cfg, "rise");
            if (rise != null)
            {
                ApplyPhase(cfg, rise, 0);
                bool a1, a2;
                byte[] m1 = Handle(cfg, rng, 3000, Frame(acu.Addr, 3, acu.Reg, 1), out a1);
                byte[] m2 = Handle(cfg, rng, 6500, Frame(acu.Addr, 3, acu.Reg, 1), out a2);
                int v1 = (m1[3] << 8) | m1[4];
                int v2 = (m2[3] << 8) | m2[4];
                r = Check("ramp midpoint", a1 && v1 >= 500 && v1 <= 590, "raw=" + v1 + " expect ~546");
                if (r == 1) pass++; else fail++;
                r = Check("ramp end 83.2", a2 && v2 == 832, "raw=" + v2);
                if (r == 1) pass++; else fail++;
            }
            Phase rec = FindPhase(cfg, "recover");
            if (rec != null)
            {
                ApplyPhase(cfg, rec, 8000);
                bool a3;
                byte[] m3 = Handle(cfg, rng, 13500, Frame(acu.Addr, 3, acu.Reg, 1), out a3);
                int v3 = (m3[3] << 8) | m3[4];
                r = Check("recover 26.0", a3 && v3 == 260, "raw=" + v3);
                if (r == 1) pass++; else fail++;
            }
        }

        Console.WriteLine(string.Format(Inv, "selftest: pass={0} fail={1}", pass, fail));
        return fail == 0 ? 0 : 1;
    }
}
'@

Add-Type -TypeDefinition $csharp -Language CSharp -ErrorAction Stop

if ($SelfTest) {
  $rc = [VgVideoSlave]::SelfTest($cfgText)
  exit $rc
}

$rc = [VgVideoSlave]::Run(
  $cfgText, $Port, $ControlPath, $LogPath,
  $Hours, $EnableRts.IsPresent, $EnableDtr.IsPresent,
  $ReplyDelayMs, $Seed, $Auto.IsPresent, $AutoDelaySec)
exit $rc
