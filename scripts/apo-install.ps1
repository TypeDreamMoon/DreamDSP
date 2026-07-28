# Puts the APO on one endpoint. Requires elevation.
#
# Scoped deliberately: one endpoint, chosen because it is active but not the
# one anyone is listening to. A fault in an APO takes out audiodg.exe and with
# it every sound on the machine, and after ten such failures Windows sets
# PKEY_Endpoint_Disable_SysFx and silently switches off system effects for that
# endpoint -- including other vendors'. So this touches as little as possible.
#
# Undo with apo-uninstall.ps1, or import the .reg in backup\.

param(
    [string]$EndpointGuid = '{393e5226-e058-4258-9f28-cd0267d925f6}',
    [string]$Dll = 'I:\Qt\DreamDSP\build\dreamdsp\DreamDspApo.dll'
)

$ErrorActionPreference = 'Stop'
$clsid = '{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}'
$target = 'C:\ProgramData\DreamDSP'

Write-Host "DreamDSP APO install"
Write-Host "  endpoint $EndpointGuid"

if (-not (Test-Path $Dll)) { throw "DLL not found: $Dll" }

# --- 1. stage the payload somewhere audiodg can actually read ---------------
# audiodg.exe runs as LOCAL SERVICE. A user's project directory is usually not
# readable by it, and the failure would be a silent load error inside a system
# process with nowhere to report it.
New-Item -ItemType Directory -Force $target | Out-Null
$staged = Join-Path $target 'DreamDspApo.dll'
Copy-Item $Dll $staged -Force
Write-Host "  staged -> $staged"

# The APO writes its diagnostic log here; LOCAL SERVICE must be able to.
& icacls.exe $target /grant '*S-1-5-19:(OI)(CI)M' /T | Out-Null   # LOCAL SERVICE
& icacls.exe $target /grant '*S-1-5-32-545:(OI)(CI)M' /T | Out-Null  # Users
Write-Host "  granted LOCAL SERVICE write access"

# --- 2. register the COM server machine-wide --------------------------------
# HKLM, not HKCU: audiodg runs under a different account and would never see a
# per-user registration.
$key = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
New-Item -Path $key -Force | Out-Null
Set-ItemProperty $key -Name '(default)' -Value 'DreamDSP Effects APO'
New-Item -Path "$key\InprocServer32" -Force | Out-Null
Set-ItemProperty "$key\InprocServer32" -Name '(default)' -Value $staged
Set-ItemProperty "$key\InprocServer32" -Name 'ThreadingModel' -Value 'Both'
Write-Host "  registered CLSID in HKLM"

# --- 3. attach it to the endpoint -------------------------------------------
# pid2 is the post-mix slot, the same one Equalizer APO uses on this machine --
# a configuration already proven to load here.
$fx = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\$EndpointGuid\FxProperties"
if (-not (Test-Path $fx)) {
    New-Item -Path $fx -Force | Out-Null
    Write-Host "  created FxProperties"
}
Set-ItemProperty $fx -Name '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2' -Value $clsid -Type String
Set-ItemProperty $fx -Name '{0f8412d3-dc5c-4db3-b174-dc47a859435c},0' -Value 1 -Type DWord
Write-Host "  attached as post-mix (pid2)"

Write-Host ""
Write-Host "Installed. The APO loads when a stream next opens that endpoint;"
Write-Host "no service restart is forced, so nothing currently playing is cut."
Write-Host "Watch $target\apo.log to see whether it was loaded."
