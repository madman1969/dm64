# Builds Dungeon Master 64 from the Atari ST disk image in this folder.
#
#   .\build.ps1            convert the game data, compile, and make bin\dm64.d64
#   .\build.ps1 -Run       ...then run it in VICE with a 512K REU
#
# Needs Python 3, cc65 (C:\cc65), VICE (for c1541 and x64sc) and the FarMem library from Tedide's
# samples. Paths can be overridden with the parameters below.
param(
    [switch]$Run,
    [string]$Stx = "Dungeon Master.stx",
    [string]$Cc65 = "C:\cc65\bin",
    [string]$Vice = "C:\GTK3VICE-3.9-win64\bin",
    [string]$FarMem = "..\Tedide\samples\FarMem"
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force bin, obj | Out-Null

python -I tools\dmconv.py $Stx gen bin
if ($LASTEXITCODE) { throw "dmconv failed" }

$sources = @(
    "src\main.c", "src\dungeon.c", "src\hw.c", "src\view.c", "src\ui.c", "src\party.c",
    "src\sensor.c", "src\creature.c", "src\save.c", "gen\dmdata.c",
    "$FarMem\src\farmem.c", "$FarMem\src\faralloc.c", "$FarMem\src\farhw.s"
)
& "$Cc65\cl65.exe" -t c64 -O -Cl -C c64-dm64.cfg -I include -I gen -I "$FarMem\include" `
    --obj-path obj -m bin\dm64.map -o bin\dm64.prg @sources
if ($LASTEXITCODE) { throw "cl65 failed" }

# Saved games on the old disk image survive the rebuild: copied off, and back onto the new one.
$saves = @()
if (Test-Path bin\dm64.d64) {
    foreach ($slot in "dmsave1", "dmsave2") {
        Remove-Item "obj\$slot" -ErrorAction SilentlyContinue
        & "$Vice\c1541.exe" -attach bin\dm64.d64 -read $slot "obj\$slot" 2>&1 | Out-Null
        if (Test-Path "obj\$slot") { $saves += "-write", "obj\$slot", $slot }
    }
}
& "$Vice\c1541.exe" -format "dungeon master,64" d64 bin\dm64.d64 `
    -write bin\dm64.prg dm64 -write bin\dmhi.bin dmhi -write bin\dmlev.bin dmlev @saves | Out-Null
if ($LASTEXITCODE) { throw "c1541 failed" }
if ($saves) { Write-Host "Kept saved games:" ($saves | Where-Object { $_ -like "dmsave*" }) }
Write-Host "Built bin\dm64.d64"

if ($Run) {
    & "$Vice\x64sc.exe" -reu -reusize 512 -autostart bin\dm64.d64
}
