# patch_pe.ps1 - force a PE32 image's OS and subsystem version down to 5.0
# so the Windows loader will run it on Windows 2000.  The VS2022 linker
# refuses /SUBSYSTEM:WINDOWS,5.00, so we patch the optional header directly.
param([Parameter(Mandatory=$true)][string]$Path)

$bytes = [System.IO.File]::ReadAllBytes($Path)
$elfanew = [System.BitConverter]::ToInt32($bytes, 0x3C)
if ($bytes[$elfanew] -ne 0x50 -or $bytes[$elfanew+1] -ne 0x45) {
    Write-Error "Not a valid PE file"; exit 1
}
$opt = $elfanew + 24
$magic = [System.BitConverter]::ToUInt16($bytes, $opt)
if ($magic -ne 0x10B) { Write-Error "Not a PE32 (32-bit) image (magic=0x$($magic.ToString('X')))"; exit 1 }

function SetWord([int]$off, [int]$val) {
    $script:bytes[$off]   = [byte]($val -band 0xFF)
    $script:bytes[$off+1] = [byte](($val -shr 8) -band 0xFF)
}

SetWord ($opt + 40) 5   # MajorOperatingSystemVersion
SetWord ($opt + 42) 0   # MinorOperatingSystemVersion
SetWord ($opt + 48) 5   # MajorSubsystemVersion
SetWord ($opt + 50) 0   # MinorSubsystemVersion

[System.IO.File]::WriteAllBytes($Path, $bytes)
Write-Host "Patched OS/subsystem version to 5.0 -> $Path"
