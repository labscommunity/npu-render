$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Security.Principal;
public static class NrLocalPipeIntegrity {
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentThread();
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr process,uint access,out IntPtr token);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenThreadToken(IntPtr thread,uint access,bool self,out IntPtr token);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateTokenEx(IntPtr existing,uint access,IntPtr attributes,int level,int type,out IntPtr token);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool SetTokenInformation(IntPtr token,int kind,IntPtr data,uint size);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool GetTokenInformation(IntPtr token,int kind,IntPtr data,uint size,out uint required);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool SetThreadToken(IntPtr thread,IntPtr token);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool RevertToSelf();
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ConvertStringSecurityDescriptorToSecurityDescriptor(string text,uint revision,out IntPtr data,out uint size);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
 [StructLayout(LayoutKind.Sequential)] struct SA { public int length; public IntPtr descriptor; public int inherit; }
 [StructLayout(LayoutKind.Sequential)] struct Label { public IntPtr sid; public uint attributes; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateNamedPipe(string name,uint access,uint mode,uint instances,uint output,uint input,uint timeout,ref SA attributes);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFile(string name,uint access,uint sharing,IntPtr attributes,uint creation,uint flags,IntPtr template);
 static void Require(bool v) { if(!v)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()); }
 static string Integrity(IntPtr token) {
  uint n;GetTokenInformation(token,25,IntPtr.Zero,0,out n);if(n==0 || n>65536)throw new Exception("Invalid integrity label length");
  IntPtr p=Marshal.AllocHGlobal((int)n);try{Require(GetTokenInformation(token,25,p,n,out n));return new SecurityIdentifier(Marshal.ReadIntPtr(p)).Value;}finally{Marshal.FreeHGlobal(p);}
 }
 public static string[] Run(int rid) {
  IntPtr primary=IntPtr.Zero,reduced=IntPtr.Zero,effective=IntPtr.Zero,sd=IntPtr.Zero,buffer=IntPtr.Zero,pipe=new IntPtr(-1),client=new IntPtr(-1);bool impersonating=false;
  try {
   Require(OpenProcessToken(GetCurrentProcess(),10,out primary));string original=Integrity(primary);
   Require(DuplicateTokenEx(primary,0x8|0x80|0x4,IntPtr.Zero,2,2,out reduced));
   SecurityIdentifier sid=new SecurityIdentifier("S-1-16-"+rid);byte[] bytes=new byte[sid.BinaryLength];sid.GetBinaryForm(bytes,0);
   int head=Marshal.SizeOf(typeof(Label)),length=head+bytes.Length;buffer=Marshal.AllocHGlobal(length);
   Marshal.Copy(bytes,0,IntPtr.Add(buffer,head),bytes.Length);Label label=new Label();label.sid=IntPtr.Add(buffer,head);label.attributes=0x20;Marshal.StructureToPtr(label,buffer,false);
   Require(SetTokenInformation(reduced,25,buffer,(uint)length));if(Integrity(reduced)!=sid.Value)throw new Exception("Reduced token identity mismatch");
   string acl="D:P(A;;GA;;;SY)(A;;GA;;;"+WindowsIdentity.GetCurrent().User.Value+")";uint n;
   Require(ConvertStringSecurityDescriptorToSecurityDescriptor(acl,1,out sd,out n));SA sa=new SA();sa.length=Marshal.SizeOf(typeof(SA));sa.descriptor=sd;
   string name=@"\\.\pipe\npu-render-integrity-"+Guid.NewGuid().ToString("N");
   pipe=CreateNamedPipe(name,3|0x00080000,8,1,512,512,1000,ref sa);Require(pipe!=new IntPtr(-1));
   Require(SetThreadToken(IntPtr.Zero,reduced));impersonating=true;
   Require(OpenThreadToken(GetCurrentThread(),8,true,out effective));string actual=Integrity(effective);if(actual!=sid.Value)throw new Exception("Effective integrity mismatch");
   client=CreateFile(name,0xc0000000,0,IntPtr.Zero,3,0x00110000,IntPtr.Zero);int error=client==new IntPtr(-1)?Marshal.GetLastWin32Error():0;
   Require(RevertToSelf());impersonating=false;
   if(Integrity(primary)!=original)throw new Exception("Original process token changed");
   return new string[]{original,actual,(client!=new IntPtr(-1)).ToString(),error.ToString()};
  } finally {
   if(impersonating && !RevertToSelf())Environment.FailFast("Cannot restore impersonation");
   if(client!=new IntPtr(-1))CloseHandle(client);if(pipe!=new IntPtr(-1))CloseHandle(pipe);if(sd!=IntPtr.Zero)LocalFree(sd);if(buffer!=IntPtr.Zero)Marshal.FreeHGlobal(buffer);
   if(effective!=IntPtr.Zero)CloseHandle(effective);if(reduced!=IntPtr.Zero)CloseHandle(reduced);if(primary!=IntPtr.Zero)CloseHandle(primary);
  }
 }
}
'@
$cases=@(8192,4096|ForEach-Object {$rid=$_;$v=[NrLocalPipeIntegrity]::Run($rid);[ordered]@{requested_integrity_rid=$rid;server_integrity_sid=$v[0];effective_client_thread_integrity_sid=$v[1];opened=($v[2] -eq 'True');win32_error=[int]$v[3]}})
$passed=$cases.Count -eq 2 -and $cases[0].opened -and !$cases[1].opened -and $cases[1].win32_error -eq 5
[ordered]@{utc=[DateTime]::UtcNow.ToString('o');passed=$passed;cases=$cases;worker_dacl_pattern_reproduced=$true;current_process_user_identity_retained=$true;reduced_thread_tokens_only=$true;real_npu_or_native_client_tested=$false;separate_process_or_dwm_account_tested=$false;system_security_configuration_changed=$false}|ConvertTo-Json -Depth 5|Tee-Object -FilePath "$env:LOCALAPPDATA\npu-render\vm\local-pipe-integrity-v1.json"
if(!$passed){throw 'Local pipe integrity expectations failed'}
