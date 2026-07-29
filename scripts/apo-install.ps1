# Puts the APO on one endpoint. Requires elevation.
#
# Scoped deliberately: one endpoint, chosen because it is active but not the
# one anyone is listening to. A fault in an APO takes out audiodg.exe and with
# it every sound on the machine, and after ten such failures Windows sets
# PKEY_Endpoint_Disable_SysFx and silently switches off system effects for that
# endpoint -- including other vendors'. So this touches as little as possible.
#
# Undo with apo-uninstall.ps1, or import the .reg in backup\.

#
# Steps 1 and 2 (staging the DLL, registering the CLSID) need elevation because
# they write Program Data and HKLM\Software\Classes. Step 3 does not: on these
# endpoints BUILTIN\Users is granted QueryValues and SetValue, so attaching the
# APO is a plain user-level write. -AttachOnly runs just that step, which is
# what a re-run needs once the first two have already been done.
#
param(
    [string]$EndpointGuid = '{393e5226-e058-4258-9f28-cd0267d925f6}',
    [string]$Dll = 'I:\Qt\DreamDSP\build\dreamdsp\DreamDspApo.dll',
    [switch]$AttachOnly
)

$ErrorActionPreference = 'Stop'
$clsid = '{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}'
$target = 'C:\ProgramData\DreamDSP'

Write-Host "DreamDSP APO install"
Write-Host "  endpoint $EndpointGuid"

if ($AttachOnly) {
    Write-Host "  (attach only -- assuming the DLL is staged and the CLSID registered)"
} else {

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

# --- 2b. register with the audio engine's APO database ----------------------
# A registered CLSID is necessary but not sufficient. The engine reads an
# endpoint's FxProperties, then looks every CLSID it finds up under
# AudioEngine\AudioProcessingObjects. Anything missing from there is skipped in
# total silence: the DLL is never loaded, DllGetClassObject is never called, and
# nothing is written to the event log. Every APO on the machine -- Microsoft's,
# Realtek's, Equalizer APO's -- has an entry here.
#
# The values are the fields of APO_REG_PROPERTIES and must agree with
# g_regProperties in apo\DreamApo.cpp.
$apoKey = "HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\$clsid"
New-Item -Path $apoKey -Force | Out-Null
Set-ItemProperty $apoKey -Name 'FriendlyName' -Value 'DreamDSP Effects'
Set-ItemProperty $apoKey -Name 'Copyright'    -Value 'DreamDSP'
Set-ItemProperty $apoKey -Name 'MajorVersion' -Value 0    -Type DWord
Set-ItemProperty $apoKey -Name 'MinorVersion' -Value 1    -Type DWord
# 13 = APO_FLAG_INPLACE | FRAMESPERSECOND_MUST_MATCH | BITSPERSAMPLE_MUST_MATCH
Set-ItemProperty $apoKey -Name 'Flags'        -Value 13   -Type DWord
Set-ItemProperty $apoKey -Name 'MinInputConnections'  -Value 1 -Type DWord
Set-ItemProperty $apoKey -Name 'MaxInputConnections'  -Value 1 -Type DWord
Set-ItemProperty $apoKey -Name 'MinOutputConnections' -Value 1 -Type DWord
Set-ItemProperty $apoKey -Name 'MaxOutputConnections' -Value 1 -Type DWord
# Unlimited. Zero would mean no instance may ever be created.
Set-ItemProperty $apoKey -Name 'MaxInstances' -Value 0xFFFFFFFF -Type DWord
Set-ItemProperty $apoKey -Name 'NumAPOInterfaces' -Value 1 -Type DWord
# IID_IAudioProcessingObject
Set-ItemProperty $apoKey -Name 'APOInterface0' -Value '{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}'
Write-Host "  registered with the audio engine's APO database"

}   # end of the elevated section

# --- 3. attach it to the endpoint -------------------------------------------
# pid2 is the post-mix slot, the same one Equalizer APO uses on this machine --
# a configuration already proven to load here.
#
# Set-ItemProperty cannot be used here. It opens the key ReadWriteSubTree,
# which asks for CreateSubKey among other rights -- and on these keys
# Administrators are granted only SetValue and ReadKey (owner is SYSTEM). The
# request is denied as a whole even though writing a value is permitted.
# Opening with exactly the rights needed succeeds without touching any ACL.
#
# Equalizer APO took a different route: on the endpoint it uses, ownership has
# been transferred to Administrators and Users given FullControl. That works
# too, but it permanently loosens a system key, so it is the fallback here
# rather than the first move.
$subKey = "SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\$EndpointGuid\FxProperties"
$rights = [System.Security.AccessControl.RegistryRights]'SetValue,QueryValues'
$check  = [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree

$key = $null
try {
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($subKey, $check, $rights)
} catch {
    throw "Cannot open FxProperties for writing: $($_.Exception.Message)"
}
if (-not $key) { throw "FxProperties key not found for $EndpointGuid" }

try {
    $key.SetValue('{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2', $clsid,
                  [Microsoft.Win32.RegistryValueKind]::String)
    Write-Host "  attached as post-mix (pid2)"

    # Best effort: this flag is present on endpoints that have effects, but its
    # exact meaning is undocumented, so a failure here is not fatal.
    try {
        $key.SetValue('{0f8412d3-dc5c-4db3-b174-dc47a859435c},0', 1,
                      [Microsoft.Win32.RegistryValueKind]::DWord)
        Write-Host "  set the effects-present flag"
    } catch {
        Write-Host "  (could not set the effects-present flag; continuing)"
    }
} finally {
    $key.Close()
}

Write-Host ""
Write-Host "Installed. The APO loads when a stream next opens that endpoint;"
Write-Host "no service restart is forced, so nothing currently playing is cut."
Write-Host "Watch $target\apo.log to see whether it was loaded."
