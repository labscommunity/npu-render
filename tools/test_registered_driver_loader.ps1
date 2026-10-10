# Test dependency resolution in a fresh process, without creating a graphics device.
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or (Get-Volume -DriveLetter C).FileSystemLabel -ne 'NR_WINDOWS'){throw 'Owned disposable guest required'}
$device=@(Get-PnpDevice -Class Display -PresentOnly | Where-Object {($_ | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data -contains 'ROOT\NPURENDER_KMD_LAB'})
if($device.Count -ne 1){throw 'Expected one paired graphics adapter'}
$key=($device[0] | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_Driver).Data
$module=(Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$key" -Name UserModeDriverName).UserModeDriverName[0]
if(!$module.StartsWith('C:\Windows\System32\DriverStore\FileRepository\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($module) -ne 'nr_fill_umd.dll'){throw 'Unexpected registered module'}
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NrLoaderTest {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr AddDllDirectory(string path);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string path);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool RemoveDllDirectory(IntPtr cookie);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool FreeLibrary(IntPtr module);
}
'@
if([NrLoaderTest]::GetModuleHandle('openvino.dll') -ne [IntPtr]::Zero){throw 'Fresh process required'}
$results=@();$cookie=[IntPtr]::Zero
try {
 foreach($mode in 'driver-directory-and-system','explicit-probe-runtime'){
  if($mode -eq 'explicit-probe-runtime'){
   $runtime='C:\npu-lab\registered-runtime-v31'
   foreach($file in (Get-Content "$runtime\sha256.json" -Raw | ConvertFrom-Json)){
    if($file.name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash (Join-Path $runtime $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Runtime mismatch'}
   }
   $cookie=[NrLoaderTest]::AddDllDirectory($runtime)
   if($cookie -eq [IntPtr]::Zero){throw 'Runtime-directory setup failed'}
  }
  # DLL load directory + system32 + explicitly added user directories.
  $handle=[NrLoaderTest]::LoadLibraryEx($module,[IntPtr]::Zero,0xD00)
  $code=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
  $loaded=$handle -ne [IntPtr]::Zero
  $released=$false;if($loaded){$released=[NrLoaderTest]::FreeLibrary($handle)}
  $results += [ordered]@{mode=$mode;loaded=$loaded;win32_error=$(if($loaded){0}else{$code});released=$released}
 }
} finally {if($cookie -ne [IntPtr]::Zero){[NrLoaderTest]::RemoveDllDirectory($cookie) | Out-Null}}
[ordered]@{finished=[DateTime]::UtcNow.ToString('o');module_sha256=(Get-FileHash $module -Algorithm SHA256).Hash;cases=$results;dwm_execution_tested=$false;graphics_device_created=$false;npu_execution_tested=$false} | ConvertTo-Json -Depth 5
