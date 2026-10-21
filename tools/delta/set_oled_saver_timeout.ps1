# Reads or sets the ASUS OLED Care screen saver timer (seconds) that starts a screen saver on an
# idle OLED panel; a long trial raises it for its duration and restores the previous value after.
# The value lives under ASUS Optimization's settings; whether the running service re-reads it
# without a restart is recorded by the trial that first relies on it.
param([int]$Seconds=-1,[string]$Record='')
$k='HKLM:\SOFTWARE\ASUS\ASUS System Control Interface\AsusOptimization\ASUS Keyboard Hotkeys'
$old=(Get-ItemProperty -Path $k -Name OLEDProtectScreensaver -ErrorAction SilentlyContinue).OLEDProtectScreensaver
$protect=(Get-ItemProperty -Path $k -Name OLEDProtect -ErrorAction SilentlyContinue).OLEDProtect
if($Seconds -ge 0){ Set-ItemProperty -Path $k -Name OLEDProtectScreensaver -Value $Seconds -Type DWord }
$new=(Get-ItemProperty -Path $k -Name OLEDProtectScreensaver -ErrorAction SilentlyContinue).OLEDProtectScreensaver
$line="{""oled_protect"":$protect,""saver_timeout_before"":$old,""saver_timeout_now"":$new}"
if($Record){ Set-Content $Record $line }
[Console]::Out.WriteLine($line)
