$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Collections.Generic;
public static class NrNativeSessionSnapshot {
 [StructLayout(LayoutKind.Sequential)] public struct Session {public uint Id;public IntPtr Name;public int State;}
 [DllImport("wtsapi32.dll",SetLastError=true)] static extern bool WTSEnumerateSessionsW(IntPtr server,int reserved,int version,out IntPtr sessions,out int count);
 [DllImport("wtsapi32.dll",SetLastError=true)] static extern bool WTSQuerySessionInformationW(IntPtr server,uint id,int info,out IntPtr buffer,out uint bytes);
 [DllImport("wtsapi32.dll")] static extern void WTSFreeMemory(IntPtr buffer);
 [DllImport("kernel32.dll")] public static extern uint WTSGetActiveConsoleSessionId();
 static string Query(uint id,int kind){IntPtr p;uint bytes;if(!WTSQuerySessionInformationW(IntPtr.Zero,id,kind,out p,out bytes))return null;try{return Marshal.PtrToStringUni(p);}finally{WTSFreeMemory(p);}}
 public sealed class Row {public uint id;public string station;public int state;public bool has_user;public bool is_console;}
 public static Row[] Read(){IntPtr p;int count;if(!WTSEnumerateSessionsW(IntPtr.Zero,0,1,out p,out count))throw new System.ComponentModel.Win32Exception();try{if(count<0 || count>64)throw new Exception("Unexpected session count");var rows=new List<Row>();int size=Marshal.SizeOf(typeof(Session));for(int n=0;n<count;++n){var s=(Session)Marshal.PtrToStructure(IntPtr.Add(p,n*size),typeof(Session));rows.Add(new Row{id=s.Id,station=Marshal.PtrToStringUni(s.Name),state=s.State,has_user=!String.IsNullOrEmpty(Query(s.Id,5)),is_console=s.Id==WTSGetActiveConsoleSessionId()});}return rows.ToArray();}finally{WTSFreeMemory(p);}}
}
'@
$sessions=@([NrNativeSessionSnapshot]::Read());$owners=@()
foreach($p in @(Get-CimInstance Win32_Process -Filter "Name='dwm.exe'")){
 $sid=Invoke-CimMethod -InputObject $p -MethodName GetOwnerSid
 $owners+=[ordered]@{pid=$p.ProcessId;session_id=$p.SessionId;owner_query_result=$sid.ReturnValue;owner_sid=$sid.Sid}
}
[ordered]@{recorded=[DateTime]::UtcNow.ToString('o');sessions=$sessions;active_user_sessions=@($sessions|Where-Object {$_.has_user -and $_.state -eq 0}).Count;console_session_id=[NrNativeSessionSnapshot]::WTSGetActiveConsoleSessionId();dwm_processes=$owners;changes_performed=$false}|ConvertTo-Json -Depth 6|Set-Content "$r\vm\native-sessions-v1.json"
Get-Content "$r\vm\native-sessions-v1.json"
