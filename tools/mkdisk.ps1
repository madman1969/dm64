# Makes bin\dm64.d64 from the linked program: joins the overlays (bin\dm64.prg.1 to .4, each
# padded to the overlay size in c64-dm64.cfg) into DMOVL, then writes the program and its data
# files to a fresh disk image, keeping any saved games from the old one.
#
#   powershell -File tools\mkdisk.ps1 [-Vice C:\path\to\vice\bin]
param([string]$Vice = "C:\GTK3VICE-3.9-win64\bin")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$cfg = Get-Content c64-dm64.cfg -Raw
if ($cfg -notmatch '__OVLSIZE__:\s*type\s*=\s*weak,\s*value\s*=\s*\$([0-9A-Fa-f]+)') {
    throw "no __OVLSIZE__ in c64-dm64.cfg"
}
$size = [Convert]::ToInt32($Matches[1], 16)
$ovl = New-Object System.Collections.Generic.List[byte]
$ovl.AddRange([byte[]](0, 0))                       # a dummy load address
foreach ($n in 1..4) {
    $part = [IO.File]::ReadAllBytes("bin\dm64.prg.$n")
    if ($part.Length -gt $size) { throw "overlay $n is $($part.Length) bytes, more than $size" }
    $ovl.AddRange($part)
    $ovl.AddRange([byte[]]::new($size - $part.Length))
}
[IO.File]::WriteAllBytes("bin\dmovl.bin", $ovl.ToArray())

# Saved games on the old disk image survive: copied off, and back onto the new one.
New-Item -ItemType Directory -Force obj | Out-Null
$saves = @()
if (Test-Path bin\dm64.d64) {
    foreach ($slot in "dmsave1", "dmsave2") {
        Remove-Item "obj\$slot" -ErrorAction SilentlyContinue
        & "$Vice\c1541.exe" -attach bin\dm64.d64 -read $slot "obj\$slot" 2>&1 | Out-Null
        if (Test-Path "obj\$slot") { $saves += "-write", "obj\$slot", $slot }
    }
}
& "$Vice\c1541.exe" -format "dungeon master,64" d64 bin\dm64.d64 `
    -write bin\dm64.prg dm64 -write bin\dmhi.bin dmhi -write bin\dmlev.bin dmlev `
    -write bin\dmovl.bin dmovl -write bin\dmspr.bin dmspr -write bin\dmtxt.bin dmtxt @saves | Out-Null
if ($LASTEXITCODE) { throw "c1541 failed" }
if ($saves) { Write-Host "Kept saved games:" ($saves | Where-Object { $_ -like "dmsave*" }) }
