[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Recipes,
    [Parameter(Mandatory=$true)][string]$Cooker,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$recipeRoot = (Resolve-Path -LiteralPath $Recipes).Path
$cookerPath = (Resolve-Path -LiteralPath $Cooker).Path
$outputRoot = [IO.Path]::GetFullPath($Output)
$inputs = @(Get-ChildItem -LiteralPath $recipeRoot -Filter '*.json' -File)
if (!$inputs.Count) { throw 'No captured shader recipes were found.' }
# Native preparation owns bounded parallel compilation and atomic records.
& $cookerPath $recipeRoot $outputRoot
if ($LASTEXITCODE -ne 0) { throw "Shader preparation failed with exit code $LASTEXITCODE" }
$files = [ordered]@{}
foreach ($recipeFile in $inputs) {
    $recipe = Get-Content -LiteralPath $recipeFile.FullName -Raw | ConvertFrom-Json
    if ($recipe.schema -ne 1 -or $recipe.key -notmatch '^[0-9a-f]{16}$') {
        throw "Invalid shader recipe: $($recipeFile.Name)"
    }
    $relative = "d3d12/$($recipe.key).shader"
    $prepared = Join-Path $outputRoot $relative
    if (!(Test-Path -LiteralPath $prepared -PathType Leaf)) { throw "Missing prepared shader: $relative" }
    $files[$relative] = (Get-FileHash -LiteralPath $prepared -Algorithm SHA256).Hash.ToLowerInvariant()
}
$record = [ordered]@{
    schema = 1
    kind = 'phlosion-prepared-hlsl'
    files = $files
    recipeCount = $inputs.Count
    cookerSha256 = (Get-FileHash -LiteralPath $cookerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    preparedUtc = [DateTime]::UtcNow.ToString('o')
}
$manifest = Join-Path $outputRoot 'manifest.json'
$temp = Join-Path $outputRoot ("manifest." + [Guid]::NewGuid().ToString('N') + '.tmp')
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temp -Encoding UTF8
Move-Item -LiteralPath $temp -Destination $manifest -Force
Write-Output "Prepared $($files.Count) shaders. Ship manifest.json and d3d12; the reusable compiler cache stays outside the package."
