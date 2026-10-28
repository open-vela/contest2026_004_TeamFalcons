# Diagnostic Modbus RTU slave for the existing 14-point room scene.
# Only replies to board FC03/FC04 reads; never initiates bus traffic.
param(
  [string]$ComPort = 'COM4',
  [string]$ControlPath = '',
  [string]$LogPath = '',
  [switch]$EnableRts,
  [switch]$EnableDtr,
  [ValidateRange(1,168)][int]$Hours = 24,
  # Default to prompt replies so tests can expose receive timing defects.
  # Nonzero delays are only for explicit timing/timeout experiments.
  [ValidateRange(0,1000)][int]$ReplyDelayMs = 0
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
if (!$ControlPath) { $ControlPath = Join-Path $PSScriptRoot '../.debug/maintenance-slave-mode.txt' }
if (!$LogPath) { $LogPath = Join-Path $PSScriptRoot '../.debug/maintenance-slave.log' }
$ControlPath = [IO.Path]::GetFullPath($ControlPath)
$LogPath = [IO.Path]::GetFullPath($LogPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LogPath)) | Out-Null
# Modes: normal, warn (temp=30 C), critical (temp=36 C), offline
# (all slave responses suppressed), offline1 (only slave 1 suppressed), stop.
if (!(Test-Path -LiteralPath $ControlPath)) { [IO.File]::WriteAllText($ControlPath,'normal') }
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.IO.Ports;
using System.Diagnostics;
using System.Collections.Generic;
using System.Threading;
public static class VelaMaintenanceSlave {
 static ushort Crc(byte[] a,int n) { ushort c=0xffff; for(int i=0;i<n;i++){c^=a[i];for(int j=0;j<8;j++)c=(ushort)(((c&1)!=0)?(c>>1)^0xa001:c>>1);}return c; }
 static int Value(int slave,int reg,string mode) {
  if(slave==1&&reg==0)return mode=="critical"?360:mode=="warn"?300:240;
  if(slave==1&&reg==1)return 500;
  if(slave==2&&reg==2)return 0;
  if(slave==3&&reg==0)return 10;
  if(slave==4&&reg==0)return 250;
  if(slave==4&&reg==1)return 240;
  if(slave==4&&reg==2)return 1;
  if(slave==5&&reg==0)return 400;
  if(slave==5&&reg==1)return 80;
  if(slave==5&&reg==2)return 0;
  if(slave==6&&reg==0)return 2300;
  if(slave==6&&reg==1)return 300;
  if(slave==6&&reg==2)return 1234;
  if(slave==7&&reg==0)return 0;
  return -1;
 }
 public static void Run(string port,string control,string log,int hours,bool rts,bool dtr,int replyDelayMs) {
  using(var w=new StreamWriter(log,true,new System.Text.UTF8Encoding(false)))
  using(var p=new SerialPort(port,9600,Parity.None,8,StopBits.One)) {
   w.AutoFlush=true;p.ReadTimeout=100;p.WriteTimeout=1000;p.RtsEnable=rts;p.DtrEnable=dtr;p.Open();
   w.WriteLine("START {0:o} port={1} mode=slave-only baud=9600 rts={2} dtr={3} reply_delay_ms={4}",DateTime.Now,port,rts,dtr,replyDelayMs);
   var timer=Stopwatch.StartNew();var rx=new List<byte>();string mode="normal";long requests=0,replies=0,ignored=0,bad=0;int rawBudget=256;long next=0,refresh=0;
   try { while(timer.Elapsed.TotalHours<hours) {
    if(timer.ElapsedMilliseconds>=refresh) {
     try { string m=File.ReadAllText(control).Trim().ToLowerInvariant();
      if(m=="stop")break;
      if(m=="normal"||m=="warn"||m=="critical"||m=="offline"||m=="offline1"){
       if(m!=mode)w.WriteLine("MODE {0:o} {1}",DateTime.Now,m);mode=m;
      }
     }catch(IOException){}refresh=timer.ElapsedMilliseconds+250;
    }
    int available=p.BytesToRead;
    if(available>0){byte[] chunk=new byte[available];int n=p.Read(chunk,0,chunk.Length);if(rawBudget>0){int sample=Math.Min(rawBudget,n);w.WriteLine("RX {0:o} {1}",DateTime.Now,BitConverter.ToString(chunk,0,sample));rawBudget-=sample;}for(int i=0;i<n;i++)rx.Add(chunk[i]);}
    while(rx.Count>=8){byte[] q=rx.GetRange(0,8).ToArray();ushort crc=Crc(q,6);
     if(q[6]!=(byte)crc||q[7]!=(byte)(crc>>8)){rx.RemoveAt(0);bad++;continue;}
     rx.RemoveRange(0,8);requests++;int s=q[0],fc=q[1],reg=(q[2]<<8)|q[3],qty=(q[4]<<8)|q[5];
     if(s<1||s>7||mode=="offline"||(mode=="offline1"&&s==1)){ignored++;continue;}
     byte error=0;if(fc!=3&&fc!=4)error=1;else if(qty<1||qty>125)error=3;
     if(error==0)for(int i=0;i<qty;i++)if(Value(s,reg+i,mode)<0){error=2;break;}
     byte[] response=error!=0?new byte[]{(byte)s,(byte)(fc|0x80),error,0,0}:new byte[5+qty*2];
     if(error==0){response[0]=(byte)s;response[1]=(byte)fc;response[2]=(byte)(qty*2);for(int i=0;i<qty;i++){int v=Value(s,reg+i,mode);response[3+i*2]=(byte)(v>>8);response[4+i*2]=(byte)v;}}
     crc=Crc(response,response.Length-2);response[response.Length-2]=(byte)crc;response[response.Length-1]=(byte)(crc>>8);
     if(replyDelayMs>0){Thread.Sleep(replyDelayMs);}
     p.Write(response,0,response.Length);replies++;
    }
    if(timer.ElapsedMilliseconds>=next){w.WriteLine("STATUS {0:o} elapsed_s={1:F1} mode={2} requests={3} replies={4} suppressed={5} crc_scan={6}",DateTime.Now,timer.Elapsed.TotalSeconds,mode,requests,replies,ignored,bad);next=timer.ElapsedMilliseconds+10000;}
    Thread.Sleep(2);
   }} finally {w.WriteLine("STOP {0:o} requests={1} replies={2} suppressed={3} crc_scan={4}",DateTime.Now,requests,replies,ignored,bad);}
  }
 }
}
'@
[VelaMaintenanceSlave]::Run($ComPort,$ControlPath,$LogPath,$Hours,$EnableRts.IsPresent,$EnableDtr.IsPresent,$ReplyDelayMs)
