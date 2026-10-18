[CmdletBinding()]
param(
    [string]$Project = '',
    [string[]]$Requested = @(),
    [ValidateSet('','windows','linux','macos','other')][string]$Platform = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Platform) {
    $Platform = if ($env:OS -eq 'Windows_NT') { 'windows' }
        elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::OSX)) { 'macos' }
        else { 'linux' }
}
if ($Project) {
    $policy = (Get-Content -LiteralPath $Project -Raw | ConvertFrom-Json).rendering
    if (-not $policy) { throw "Project has no rendering enrollment: $Project" }
    $property = $policy.PSObject.Properties[$Platform]
    if (-not $property -or -not @($property.Value).Count) { throw "Project has no renderer enrollment for $Platform" }
    $available = @($property.Value)
} else {
    $available = if ($Platform -eq 'windows') { @('d3d12','vulkan') }
        elseif ($Platform -eq 'linux') { @('vulkan') } else { @() }
}
if (-not $available.Count) { throw "No renderer enrolled for $Platform" }
if (@($available | Select-Object -Unique).Count -ne $available.Count) { throw 'Duplicate renderer enrollment' }
foreach ($api in $available) {
    if ($api -notin @('d3d12','vulkan','opengl') -or ($api -eq 'd3d12' -and $Platform -ne 'windows')) {
        throw "Invalid renderer enrollment: $api on $Platform"
    }
}
if ($Requested.Count) {
    foreach ($api in $Requested) {
        if ($api -ne 'auto' -and $api -notin $available) { throw "Renderer '$api' is not enrolled for this project on $Platform" }
    }
    return
}
$available
