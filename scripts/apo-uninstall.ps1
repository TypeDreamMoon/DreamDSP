# Removes everything apo-install.ps1 put in place. Requires elevation.
#
# Written before the installer, and deliberately tolerant: every step is
# independent and a failure in one does not stop the others, because the
# situation in which this matters is "audio is broken and I need it back".
#
# Manual fallback if this script itself cannot run:
#   1. reg import backup\MMDevices-Render-<stamp>.reg
#   2. net stop audiosrv && net start audiosrv     (as administrator)

param(
    [string]$EndpointGuid = '{393e5226-e058-4258-9f28-cd0267d925f6}'
)

$ErrorActionPreference = 'Continue'
$clsid = '{6D2F1C55-5E4B-4A7E-9C31-0D5A6C4B7E10}'

Write-Host "DreamDSP APO uninstall"
Write-Host "  endpoint $EndpointGuid"

# --- 1. take the APO off the endpoint --------------------------------------
$fx = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\$EndpointGuid\FxProperties"
if (Test-Path $fx) {
    foreach ($pid_ in 0, 1, 2) {
        $name = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},$pid_"
        $val = (Get-ItemProperty $fx -Name $name -ErrorAction SilentlyContinue).$name
        if ($val -and $val -eq $clsid) {
            Remove-ItemProperty $fx -Name $name -Force -ErrorAction SilentlyContinue
            Write-Host "  removed pid$pid_"
        }
    }
    # The enable flag is only ours if we created the key in the first place.
    Remove-ItemProperty $fx -Name '{0f8412d3-dc5c-4db3-b174-dc47a859435c},0' `
        -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "  endpoint has no FxProperties (nothing to remove)"
}

# --- 2. unregister the COM server -------------------------------------------
foreach ($root in 'HKLM:\SOFTWARE\Classes\CLSID', 'HKCU:\Software\Classes\CLSID') {
    $key = "$root\$clsid"
    if (Test-Path $key) {
        Remove-Item "$key\InprocServer32" -Force -Recurse -ErrorAction SilentlyContinue
        Remove-Item $key -Force -Recurse -ErrorAction SilentlyContinue
        Write-Host "  unregistered from $root"
    }
}

# --- 3. leave the payload, remove nothing the user might want ---------------
Write-Host "  C:\ProgramData\DreamDSP left in place (contains apo.log)"

Write-Host ""
Write-Host "Done. A stream started from now on will not load the APO."
Write-Host "If audio is still wrong, restart the audio service:"
Write-Host "  net stop audiosrv ; net start audiosrv"
