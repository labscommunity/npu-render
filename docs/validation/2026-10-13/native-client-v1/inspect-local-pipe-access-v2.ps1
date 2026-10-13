$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Security.Principal;
public static class NrPipeAccessReadOnly {
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr process,uint access,out IntPtr token);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool GetTokenInformation(IntPtr token,int kind,IntPtr data,uint size,out uint required);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ConvertStringSecurityDescriptorToSecurityDescriptor(string text,uint revision,out IntPtr data,out uint size);
 [DllImport("advapi32.dll",SetLastError=true)] static extern uint GetSecurityInfo(IntPtr handle,int type,uint information,out IntPtr owner,out IntPtr group,out IntPtr dacl,out IntPtr sacl,out IntPtr descriptor);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ConvertSecurityDescriptorToStringSecurityDescriptor(IntPtr descriptor,uint revision,uint information,out IntPtr text,out uint length);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
 [StructLayout(LayoutKind.Sequential)] struct SA { public int length; public IntPtr descriptor; public int inherit; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateNamedPipe(string name,uint access,uint mode,uint instances,uint output,uint input,uint timeout,ref SA attributes);
 public static string[] Inspect() {
  IntPtr token=IntPtr.Zero,buffer=IntPtr.Zero,sd=IntPtr.Zero,pipe=new IntPtr(-1),actual=IntPtr.Zero,text=IntPtr.Zero;
  try {
   if(!OpenProcessToken(GetCurrentProcess(),8,out token))throw new System.ComponentModel.Win32Exception();
   uint n;GetTokenInformation(token,25,IntPtr.Zero,0,out n);if(n==0 || n>65536)throw new Exception("Integrity label size rejected");
   buffer=Marshal.AllocHGlobal((int)n);if(!GetTokenInformation(token,25,buffer,n,out n))throw new System.ComponentModel.Win32Exception();
   string integrity=new SecurityIdentifier(Marshal.ReadIntPtr(buffer)).Value;
   string acl="D:P(A;;GA;;;SY)(A;;GA;;;"+WindowsIdentity.GetCurrent().User.Value+")";
   if(!ConvertStringSecurityDescriptorToSecurityDescriptor(acl,1,out sd,out n))throw new System.ComponentModel.Win32Exception();
   SA sa=new SA();sa.length=Marshal.SizeOf(typeof(SA));sa.descriptor=sd;
   pipe=CreateNamedPipe(@"\\.\pipe\npu-render-access-"+Guid.NewGuid().ToString("N"),3|0x00080000,8,1,512,512,1000,ref sa);
   if(pipe==new IntPtr(-1))throw new System.ComponentModel.Win32Exception();
   IntPtr owner,group,dacl,sacl;uint status=GetSecurityInfo(pipe,1,0x10,out owner,out group,out dacl,out sacl,out actual);
   if(status!=0)throw new System.ComponentModel.Win32Exception((int)status);
   if(!ConvertSecurityDescriptorToStringSecurityDescriptor(actual,1,0x10,out text,out n))throw new System.ComponentModel.Win32Exception();
   return new string[]{integrity,Marshal.PtrToStringUni(text)};
  } finally {if(text!=IntPtr.Zero)LocalFree(text);if(actual!=IntPtr.Zero)LocalFree(actual);if(pipe!=new IntPtr(-1))CloseHandle(pipe);if(sd!=IntPtr.Zero)LocalFree(sd);if(buffer!=IntPtr.Zero)Marshal.FreeHGlobal(buffer);if(token!=IntPtr.Zero)CloseHandle(token);}
 }
}
'@
$v=[NrPipeAccessReadOnly]::Inspect()
[ordered]@{utc=[DateTime]::UtcNow.ToString('o');current_process_integrity_sid=$v[0];owned_test_pipe_label_sddl=$v[1];worker_dacl_pattern_reproduced=$true;real_worker_or_renderer_started=$false;interactive_user_present=[bool](Get-CimInstance Win32_ComputerSystem).UserName;system_configuration_changed=$false}|ConvertTo-Json|Tee-Object -FilePath "$env:LOCALAPPDATA\npu-render\vm\local-pipe-access-v2.json"
