# Registers the APO with the audio engine's object database. Requires elevation.
#
# Registering the CLSID under HKLM\SOFTWARE\Classes\CLSID is necessary but not
# sufficient. When the engine builds an endpoint's effect chain it reads the
# CLSIDs out of that endpoint's FxProperties and looks each one up under
#
#     HKLM\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\{clsid}
#
# A CLSID that is not in there is skipped in complete silence: the DLL is never
# loaded, DllGetClassObject is never reached, and nothing lands in the event
# log. From the outside it is indistinguishable from an endpoint that has no
# effects configured at all -- which is exactly how this cost an afternoon.
#
# Every APO on a Windows machine has an entry here: Microsoft's own, Realtek's,
# Equalizer APO's, ViPER4Windows'. APOs built on the WDK base classes get it
# from CRegAPOProperties::Register(); this one hand-rolls its COM, so it writes
# the values itself. They are the fields of APO_REG_PROPERTIES and must agree
# with g_regProperties in apo\DreamApo.cpp.
#
# Elevation is genuinely required: BUILTIN\Users holds only ReadKey on
# AudioProcessingObjects, so unlike the FxProperties write there is no
# precise-rights trick that avoids the prompt.
#
#     powershell -ExecutionPolicy Bypass -File scripts\apo-register-engine.ps1
#
param(
    [switch]$Remove,
    [switch]$NoRestart
)

$ErrorActionPreference = 'Stop'

$isAdmin = ([Security.Principal.WindowsPrincipal] `
            [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    throw 'This script needs an elevated shell (Run as administrator).'
}

$clsid  = '{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}'
$apoKey = "HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\$clsid"

if ($Remove) {
    if (Test-Path $apoKey) {
        Remove-Item $apoKey -Recurse -Force
        Write-Host "removed $apoKey"
    } else {
        Write-Host 'nothing to remove'
    }
} else {
    New-Item -Path $apoKey -Force | Out-Null

    Set-ItemProperty $apoKey -Name 'FriendlyName' -Value 'DreamDSP Effects'
    Set-ItemProperty $apoKey -Name 'Copyright'    -Value 'DreamDSP'
    Set-ItemProperty $apoKey -Name 'MajorVersion' -Value 0 -Type DWord
    Set-ItemProperty $apoKey -Name 'MinorVersion' -Value 1 -Type DWord

    # 13 = APO_FLAG_INPLACE | FRAMESPERSECOND_MUST_MATCH | BITSPERSAMPLE_MUST_MATCH.
    # Equalizer APO registers the same value.
    Set-ItemProperty $apoKey -Name 'Flags' -Value 13 -Type DWord

    Set-ItemProperty $apoKey -Name 'MinInputConnections'  -Value 1 -Type DWord
    Set-ItemProperty $apoKey -Name 'MaxInputConnections'  -Value 1 -Type DWord
    Set-ItemProperty $apoKey -Name 'MinOutputConnections' -Value 1 -Type DWord
    Set-ItemProperty $apoKey -Name 'MaxOutputConnections' -Value 1 -Type DWord

    # -1, i.e. unlimited. Zero here does not mean "no limit", it means no
    # instance may ever be created.
    Set-ItemProperty $apoKey -Name 'MaxInstances' -Value ([int]-1) -Type DWord

    Set-ItemProperty $apoKey -Name 'NumAPOInterfaces' -Value 1 -Type DWord
    # IID_IAudioProcessingObject
    Set-ItemProperty $apoKey -Name 'APOInterface0' `
                     -Value '{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}'

    Write-Host "registered $clsid"
    (Get-ItemProperty $apoKey).PSObject.Properties |
        Where-Object { $_.Name -notmatch '^PS' } |
        ForEach-Object { Write-Host ('  {0,-22} = {1}' -f $_.Name, $_.Value) }
}

if (-not $NoRestart) {
    Write-Host ''
    Write-Host 'Restarting audiosrv (all sound cuts out for a moment)...'
    Restart-Service audiosrv -Force
    Write-Host 'done.'
}
