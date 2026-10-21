# Builds Dungeon Master 64 from the Atari ST disk image in this folder.
#
#   .\build.ps1            convert the game data, compile, and make bin\dm64.d81
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
    "src\sensor.c", "src\creature.c", "src\magic.c", "src\light.c", "src\overlay.c", "src\mouse.s", "src\irq.s", "src\bitmap.s",
    "src\recruit.c", "src\screens.c", "src\cast.c", "src\save.c", "src\inventory.c", "src\unpack.c", "src\unpackasm.s", "gen\dmdata.c",
    "$FarMem\src\farmem.c", "$FarMem\src\farhw.s"
)
& "$Cc65\cl65.exe" -t c64 -O -Cl -C c64-dm64.cfg -I include -I gen -I "$FarMem\include" `
    --obj-path obj -m bin\dm64.map -o bin\dm64.prg @sources
if ($LASTEXITCODE) { throw "cl65 failed" }

& (Join-Path $PSScriptRoot "tools\mkdisk.ps1") -Vice $Vice
Write-Host "Built bin\dm64.d81"

if ($Run) {
    & "$Vice\x64sc.exe" -reu -reusize 512 -autostart bin\dm64.d81
}
