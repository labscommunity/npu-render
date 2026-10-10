# Owned-VM identity and public Configuration Manager queries, without CIM/WMI.
function Assert-NrLabGuest {
 if([Environment]::MachineName -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Owned disposable guest required'}
}
function Get-NrLabDevice {
 param([Parameter(Mandatory=$true)][ValidateSet('ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB')][string]$HardwareId)
 Assert-NrLabGuest
 if(-not ('NrLabDeviceStatus' -as [type])){
  Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NrLabDeviceStatus {
 [DllImport("cfgmgr32.dll",CharSet=CharSet.Unicode)] public static extern uint CM_Locate_DevNodeW(out uint devinst,string id,uint flags);
 [DllImport("cfgmgr32.dll")] public static extern uint CM_Get_DevNode_Status(out uint status,out uint problem,uint devinst,uint flags);
}
'@
 }
 $root='HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\DISPLAY'
 foreach($key in (Get-ChildItem $root -ErrorAction SilentlyContinue)){
  $record=Get-ItemProperty $key.PSPath
  if($record.HardwareID -notcontains $HardwareId){continue}
  $instance='ROOT\DISPLAY\'+$key.PSChildName
  [uint32]$devinst=0
  $locate=[NrLabDeviceStatus]::CM_Locate_DevNodeW([ref]$devinst,$instance,0)
  if($locate -eq 13){continue} # CR_NO_SUCH_DEVNODE: retained phantom registry entry.
  if($locate){throw "Cannot locate owned devnode: $locate"}
  [uint32]$status=0;[uint32]$problem=0
  $result=[NrLabDeviceStatus]::CM_Get_DevNode_Status([ref]$status,[ref]$problem,$devinst,0)
  if($result){throw "Cannot query owned devnode: $result"}
  if($record.Driver -notmatch '^\{4d36e968-e325-11ce-bfc1-08002be10318\}\\\d{4}$'){throw 'Unexpected display class registration'}
  $driver=Get-ItemProperty ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\'+$record.Driver)
  if($driver.InfPath -notmatch '^oem\d+\.inf$'){throw 'Owned OEM INF unavailable'}
  [pscustomobject]@{instance=$instance;hardware_id=$HardwareId;inf=$driver.InfPath;version=$driver.DriverVersion;status=$status;problem=$problem;started=($problem -eq 0 -and ($status -band 8) -ne 0);user_mode_driver=$driver.UserModeDriverName;driver_key=$record.Driver}
 }
}
