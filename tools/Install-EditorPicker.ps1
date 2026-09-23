[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$launcher = Join-Path $PSScriptRoot 'Launch-EditorPicker.ps1'
$editorIcon = Join-Path $PSScriptRoot 'branding/phlosion-mark.ico'
$powershell = Join-Path $PSHOME 'powershell.exe'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "Missing launcher: $launcher" }
if (-not (Test-Path -LiteralPath $powershell -PathType Leaf)) { throw "Missing Windows PowerShell: $powershell" }

$shell = New-Object -ComObject WScript.Shell
$targets = @(
    [Environment]::GetFolderPath('DesktopDirectory'),
    [Environment]::GetFolderPath('Programs')
)
foreach ($directory in $targets) {
    if (-not $directory) { throw 'Windows did not provide a shortcut folder.' }
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $path = Join-Path $directory 'Phlosion Editor.lnk'
    $shortcut = $shell.CreateShortcut($path)
    $shortcut.TargetPath = $powershell
    $shortcut.Arguments = "-NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$launcher`""
    $shortcut.WorkingDirectory = $engineRoot
    $shortcut.Description = 'Choose a game project and open Phlosion Editor'
    if (-not (Test-Path -LiteralPath $editorIcon -PathType Leaf)) { throw "Missing Phlosion icon: $editorIcon" }
    $shortcut.IconLocation = "$editorIcon,0"
    $shortcut.Save()
    Write-Output $path
}
