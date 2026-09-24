[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Launcher,
    [Parameter(Mandatory = $true)][string]$LogBase,
    [switch]$Detached
)

$ErrorActionPreference = 'Stop'
$outputLog = "$LogBase.out.log"
$errorLog = "$LogBase.err.log"
try {
    if ($Detached) {
        & $Launcher -Detached 1> $outputLog 2> $errorLog
    } else {
        & $Launcher 1> $outputLog 2> $errorLog
    }
    exit 0
} catch {
    $_ | Out-String | Add-Content -LiteralPath $errorLog
    exit 1
}
