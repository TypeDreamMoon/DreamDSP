# Emergency rollback for the headphone endpoint.
#
# Restores the post-mix (MFX) slot to Equalizer APO, undoing whatever DreamDSP
# put there. Safe to run at any time, including when audio is already broken --
# it only ever writes one known-good string.
#
# Run this if the headphones go silent, crackle, or audiodg starts cycling.
#
#     powershell -ExecutionPolicy Bypass -File scripts\apo-rollback-headphones.ps1
#
# No elevation needed: BUILTIN\Users holds SetValue on this key.

$ErrorActionPreference = 'Stop'

# 后面板 耳机 / Realtek USB Audio
$endpoint = '{30d0a993-116c-4954-b341-5de5dc17de1c}'
# The value that was there before DreamDSP touched anything.
$original = '{EC1CC9CE-FAED-4822-828A-82A81A6F018F}'   # EqualizerAPO Post-Mix
$pid2     = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'

$subKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\' +
          "$endpoint\FxProperties"

# Set-ItemProperty asks for CreateSubKey rights it will not get here, so open
# the key with exactly the rights the ACL actually grants.
$rights = [System.Security.AccessControl.RegistryRights]'SetValue,QueryValues'
$check  = [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree

$key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($subKey, $check, $rights)
if (-not $key) { throw "cannot open $subKey" }

Write-Host ('was : {0}' -f $key.GetValue($pid2))
$key.SetValue($pid2, $original, [Microsoft.Win32.RegistryValueKind]::String)
Write-Host ('now : {0}' -f $key.GetValue($pid2))
$key.Close()

Write-Host ''
Write-Host 'Restarting audiosrv (all sound cuts out for a moment)...'
Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList @(
    '-NoProfile', '-Command', 'Restart-Service audiosrv -Force'
)
Write-Host 'done -- Equalizer APO is back on the post-mix slot.'
