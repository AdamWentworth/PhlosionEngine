# Adversarial CPU-only harness for the glTF scene fixture guard.
#
# The harness builds a hermetic synthetic 36-capture matrix through the guard's
# `synth` command, proves the guard accepts the pristine evidence, then mutates
# a copy of that evidence in every required way and proves the guard rejects it.
# Each mutation is restored before the pristine matrix is re-validated.
#
# Requires Windows PowerShell 5.1+ or PowerShell 7+.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [Parameter(Mandatory = $true)][string]$GuardExe,
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$processModule = Join-Path $PSScriptRoot '..\..\tools\public_asset_fixture\FixtureProcess.psm1'
Import-Module (Resolve-Path -LiteralPath $processModule).Path -Force

$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$guardPath = (Resolve-Path -LiteralPath $GuardExe).Path
$runtimeExe = Join-Path $engineRootPath "build-presentation\$Configuration\SceneFixtureRuntime.exe"
$editorExe = Join-Path $engineRootPath "build-presentation\$Configuration\PhlosionEditor.exe"
$pluginDll = Join-Path $engineRootPath "build-presentation\$Configuration\SceneFixtureEditorPlugin.dll"

$workRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('phlosion-scene-fixture-harness-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workRoot | Out-Null

$cases = @(
    @{ Name = 'a-bind'; Frame = 0 },
    @{ Name = 'a-pose'; Frame = 30 },
    @{ Name = 'failed-replacement'; Frame = 60 },
    @{ Name = 'replacement-b'; Frame = 90 },
    @{ Name = 'emission-off'; Frame = 120 },
    @{ Name = 'unloaded'; Frame = 150 }
)
$hosts = @('runtime', 'editor')
$apis = @('opengl', 'vulkan', 'd3d12')
$width = 320
$height = 180

function Invoke-Guard {
    param([string[]]$Arguments)
    return Invoke-FixtureProcess -FilePath $guardPath -Arguments $Arguments `
        -WorkingDirectory $workRoot -TimeoutSeconds 300
}

function New-SyntheticRun {
    param([string]$Path)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    foreach ($case in $cases) {
        foreach ($hostName in $hosts) {
            foreach ($api in $apis) {
                $directory = Join-Path $Path (Join-Path $hostName (Join-Path $api $case.Name))
                New-Item -ItemType Directory -Path $directory -Force | Out-Null
                $png = Join-Path $directory 'capture.png'
                $handshake = Join-Path $directory 'handshake.json'
                $synth = Invoke-Guard -Arguments @(
                    'synth', "--case=$($case.Name)", "--host=$hostName",
                    "--api=$api", "--out=$png", "--handshake=$handshake",
                    "--width=$width", "--height=$height")
                if ($synth.ExitCode -ne 0) {
                    throw "synth failed for $hostName/$api/$($case.Name): $($synth.Stderr)"
                }
                $display = @{ opengl = 'OpenGL'; vulkan = 'Vulkan'; d3d12 = 'D3D12' }[$api]
                $log = "[ParityContract][$display] PASS`n[Screenshot][$display] WROTE $png size=${width}x${height} frame=6`n"
                [System.IO.File]::WriteAllText((Join-Path $directory 'stdout.log'), $log)
                [System.IO.File]::WriteAllText((Join-Path $directory 'stderr.log'), '')
                if ($hostName -eq 'editor') {
                    $metrics = @{ schema = 'phlosion-editor-baseline-metrics-v1'; capture = @{
                        frames = 12; viewport_width = $width; viewport_height = $height };
                        renderer = @{ backend = $api }; project = @{ id = 'phlosion-scene-fixture' } }
                    [System.IO.File]::WriteAllText((Join-Path $directory 'metrics.json'),
                        ($metrics | ConvertTo-Json -Depth 6))
                }
            }
        }
    }
}

function New-Manifest {
    param([string]$RunPath, [string]$ManifestPath)
    $binaries = @()
    foreach ($entry in @(
            @{ role = 'runtime'; path = $runtimeExe },
            @{ role = 'editor'; path = $editorExe },
            @{ role = 'plugin'; path = $pluginDll },
            @{ role = 'guard'; path = $guardPath })) {
        $item = Get-Item -LiteralPath $entry.path
        $binaries += [ordered]@{
            role = $entry.role
            path = $item.FullName
            sha256 = (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant()
            bytes = [uint64]$item.Length
        }
    }
    $captures = @()
    foreach ($case in $cases) {
        foreach ($hostName in $hosts) {
            foreach ($api in $apis) {
                $directory = Join-Path $RunPath (Join-Path $hostName (Join-Path $api $case.Name))
                $png = Join-Path $directory 'capture.png'
                $handshake = Join-Path $directory 'handshake.json'
                $item = Get-Item -LiteralPath $png
                $captures += [ordered]@{
                    case = $case.Name
                    host = $hostName
                    api = $api
                    logical_frame = $case.Frame
                    exit_code = 0
                    surface = [ordered]@{ width = $width; height = $height }
                    png = $png
                    png_sha256 = (Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash.ToLowerInvariant()
                    png_bytes = [uint64]$item.Length
                    handshake = $handshake
                    handshake_sha256 = (Get-FileHash -LiteralPath $handshake -Algorithm SHA256).Hash.ToLowerInvariant()
                    stdout = (Join-Path $directory 'stdout.log')
                    stderr = (Join-Path $directory 'stderr.log')
                    metrics = (Join-Path $directory 'metrics.json')
                    requested_capture_frame = 6
                }
            }
        }
    }
    $manifest = [ordered]@{
        schema = 'phlosion-scene-fixture-matrix-v1'
        run_id = 'harness'
        source_revision = '0000000000000000000000000000000000000000'
        source_dirty = $true
        editor_frames = 12
        capture_frames = @{ runtime = 6; editor = 6 }
        thresholds = [ordered]@{
            normalized_mae = 0.01
            normalized_rmse = 0.08
            changed_ratio = 0.05
            channel_tolerance = 8
        }
        binaries = $binaries
        captures = $captures
    }
    $json = $manifest | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($ManifestPath, $json, [System.Text.UTF8Encoding]::new($false))
}

function Invoke-Matrix {
    param([string]$ManifestPath, [switch]$ExpectSuccess)
    $result = Invoke-Guard -Arguments @('matrix', "--manifest=$ManifestPath")
    $expected = if ($ExpectSuccess) { 0 } else { 1 }
    if ($result.ExitCode -ne $expected) {
        throw ("guard matrix returned $($result.ExitCode), expected $expected for $ManifestPath`n" +
               $result.Stdout + "`n" + $result.Stderr)
    }
    return $result
}

try {
    $base = Join-Path $workRoot 'base'
    New-SyntheticRun -Path $base
    $baseManifest = Join-Path $base 'manifest.json'
    New-Manifest -RunPath $base -ManifestPath $baseManifest
    Invoke-Matrix -ManifestPath $baseManifest -ExpectSuccess | Out-Null
    Write-Host '[SceneFixture][Harness] pristine matrix accepted'

    $mutations = @(
        @{ Name = 'duplicate-case'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.captures = @($manifest.captures) + @($manifest.captures[0])
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'missing-case'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.captures = @($manifest.captures | Where-Object {
                    -not ($_.case -eq 'a-pose' -and $_.host -eq 'editor' -and $_.api -eq 'd3d12') })
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'unknown-host'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.captures[0].host = 'standalone'
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'wrong-api'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.captures[0].api = 'vulkan'
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'wrong-frame'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.captures[0].logical_frame = 999
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'stale-binary-hash'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                ($manifest.binaries | Where-Object { $_.role -eq 'guard' }).sha256 =
                    ('0' * 64)
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'stale-binary-size'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                ($manifest.binaries | Where-Object { $_.role -eq 'runtime' }).bytes = [uint64]1
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'loose-thresholds'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.thresholds.normalized_mae = 0.5
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'outside-run-path'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $manifest.captures[0].png = (Join-Path ([System.IO.Path]::GetTempPath()) 'escape.png')
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'missing-image'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                Remove-Item -LiteralPath $manifest.captures[0].png -Force
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'mutated-image'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $png = $manifest.captures[0].png
                $blank = Invoke-Guard -Arguments @(
                    'blank', "--out=$png", "--width=$width", "--height=$height")
                if ($blank.ExitCode -ne 0) { throw $blank.Stderr }
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'unloaded-with-content'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $entry = $manifest.captures | Where-Object { $_.case -eq 'unloaded' } |
                    Select-Object -First 1
                $blank = Invoke-Guard -Arguments @(
                    'synth', '--case=a-pose', "--out=$($entry.png)",
                    "--width=$width", "--height=$height")
                if ($blank.ExitCode -ne 0) { throw $blank.Stderr }
                $entry.png_sha256 =
                    (Get-FileHash -LiteralPath $entry.png -Algorithm SHA256).Hash.ToLowerInvariant()
                $entry.png_bytes = [uint64](Get-Item -LiteralPath $entry.png).Length
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'missing-scene-content'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $entry = $manifest.captures | Where-Object {
                    $_.case -eq 'a-pose' -and $_.host -eq 'runtime' -and $_.api -eq 'opengl' } |
                    Select-Object -First 1
                $blank = Invoke-Guard -Arguments @(
                    'blank', "--out=$($entry.png)", "--width=$width", "--height=$height")
                if ($blank.ExitCode -ne 0) { throw $blank.Stderr }
                $entry.png_sha256 =
                    (Get-FileHash -LiteralPath $entry.png -Algorithm SHA256).Hash.ToLowerInvariant()
                $entry.png_bytes = [uint64](Get-Item -LiteralPath $entry.png).Length
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'swapped-emission-case'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $emission = $manifest.captures | Where-Object {
                    $_.case -eq 'emission-off' -and $_.host -eq 'runtime' -and $_.api -eq 'opengl' } |
                    Select-Object -First 1
                $blank = Invoke-Guard -Arguments @(
                    'synth', '--case=a-pose', "--out=$($emission.png)",
                    "--width=$width", "--height=$height")
                if ($blank.ExitCode -ne 0) { throw $blank.Stderr }
                $emission.png_sha256 =
                    (Get-FileHash -LiteralPath $emission.png -Algorithm SHA256).Hash.ToLowerInvariant()
                $emission.png_bytes = [uint64](Get-Item -LiteralPath $emission.png).Length
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'handshake-host'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $handshakePath = $manifest.captures[0].handshake
                $handshake = Get-Content -LiteralPath $handshakePath -Raw | ConvertFrom-Json
                $handshake.host = 'standalone'
                [System.IO.File]::WriteAllText($handshakePath, ($handshake | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
                $manifest.captures[0].handshake_sha256 =
                    (Get-FileHash -LiteralPath $handshakePath -Algorithm SHA256).Hash.ToLowerInvariant()
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'handshake-replacement-state'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $entry = $manifest.captures | Where-Object { $_.case -eq 'failed-replacement' } |
                    Select-Object -First 1
                $handshake = Get-Content -LiteralPath $entry.handshake -Raw | ConvertFrom-Json
                $handshake.replacement.failed = $false
                $handshake.replacement.diagnostic = ''
                [System.IO.File]::WriteAllText($entry.handshake,
                    ($handshake | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
                $entry.handshake_sha256 =
                    (Get-FileHash -LiteralPath $entry.handshake -Algorithm SHA256).Hash.ToLowerInvariant()
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } },
        @{ Name = 'handshake-region'; Apply = {
                $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
                $entry = $manifest.captures[0]
                $handshake = Get-Content -LiteralPath $entry.handshake -Raw | ConvertFrom-Json
                $handshake.regions.character = @(0.4, 0.4, 0.1, 0.1)
                [System.IO.File]::WriteAllText($entry.handshake,
                    ($handshake | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
                $entry.handshake_sha256 =
                    (Get-FileHash -LiteralPath $entry.handshake -Algorithm SHA256).Hash.ToLowerInvariant()
                [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                    [System.Text.UTF8Encoding]::new($false))
            } }
    )

    foreach ($name in @('missing-metrics', 'missing-stdout', 'overflow-frame',
            'overflow-dimensions', 'fractional-bytes', 'duplicate-binary',
            'wrong-capture-log', 'missing-plugin-activation', 'emission-missing-vulkan-d3d12',
            'probe-no-factor', 'probe-double-factor', 'probe-wrong-transfer')) {
        $mutations += @{ Name = $name; Apply = {
            $manifest = Get-Content -LiteralPath $baseManifest -Raw | ConvertFrom-Json
            $entry = $manifest.captures | Where-Object { $_.host -eq 'editor' } | Select-Object -First 1
            switch ($mutation.Name) {
                'missing-metrics' { $entry.PSObject.Properties.Remove('metrics') }
                'missing-stdout' { $entry.PSObject.Properties.Remove('stdout') }
                'overflow-frame' { $entry.logical_frame = [long]$entry.logical_frame + 4294967296 }
                'overflow-dimensions' { $entry.surface.width = [long]$width + 4294967296 }
                'fractional-bytes' { $manifest.binaries[0].bytes = [double]$manifest.binaries[0].bytes + 0.5 }
                'duplicate-binary' { $manifest.binaries = @($manifest.binaries) + @($manifest.binaries[0]) }
                'wrong-capture-log' {
                    $log = [System.IO.File]::ReadAllText($entry.stdout).Replace('frame=6', 'frame=5')
                    [System.IO.File]::WriteAllText($entry.stdout, $log)
                }
                'missing-plugin-activation' {
                    $metrics = Get-Content -LiteralPath $entry.metrics -Raw | ConvertFrom-Json
                    $metrics.project.id = 'wrong-project'
                    [System.IO.File]::WriteAllText($entry.metrics, ($metrics | ConvertTo-Json -Depth 6))
                }
                'emission-missing-vulkan-d3d12' {
                    foreach ($row in @($manifest.captures | Where-Object { $_.case -eq 'emission-off' -and $_.api -ne 'opengl' })) {
                        $result = Invoke-Guard -Arguments @('synth', '--case=a-pose', "--out=$($row.png)", "--width=$width", "--height=$height")
                        if ($result.ExitCode -ne 0) { throw $result.Stderr }
                        $row.png_sha256 = (Get-FileHash -LiteralPath $row.png -Algorithm SHA256).Hash.ToLowerInvariant()
                        $row.png_bytes = [uint64](Get-Item -LiteralPath $row.png).Length
                    }
                }
                default {
                    foreach ($row in @($manifest.captures | Where-Object { $_.case -eq 'a-bind' })) {
                        $result = Invoke-Guard -Arguments @('synth', '--case=a-bind', "--defect=$($mutation.Name)", "--out=$($row.png)", "--width=$width", "--height=$height")
                        if ($result.ExitCode -ne 0) { throw $result.Stderr }
                        $row.png_sha256 = (Get-FileHash -LiteralPath $row.png -Algorithm SHA256).Hash.ToLowerInvariant()
                        $row.png_bytes = [uint64](Get-Item -LiteralPath $row.png).Length
                    }
                }
            }
            [System.IO.File]::WriteAllText($baseManifest, ($manifest | ConvertTo-Json -Depth 10),
                [System.Text.UTF8Encoding]::new($false))
        } }
    }

    $pristineManifest = Get-Content -LiteralPath $baseManifest -Raw
    $backup = Join-Path $workRoot 'pristine'
    Copy-Item -LiteralPath $base -Destination $backup -Recurse
    foreach ($mutation in $mutations) {
        & $mutation.Apply
        Invoke-Matrix -ManifestPath $baseManifest | Out-Null
        Write-Host "[SceneFixture][Harness] rejected $($mutation.Name)"
        # Restore the pristine evidence before the next mutation.
        [System.IO.File]::WriteAllText($baseManifest, $pristineManifest,
            [System.Text.UTF8Encoding]::new($false))
        Get-ChildItem -LiteralPath $backup -File -Recurse | ForEach-Object {
            $relative = $_.FullName.Substring($backup.Length + 1)
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $base $relative) -Force
        }
    }

    Invoke-Matrix -ManifestPath $baseManifest -ExpectSuccess | Out-Null
    Write-Host '[SceneFixture][Harness] pristine matrix accepted after every mutation'
    Write-Host '[SceneFixture][Harness] PASS'
    exit 0
} catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    Write-Host '[SceneFixture][Harness] FAILED'
    exit 1
} finally {
    # Removes only the directory this harness created.
    $resolvedWork = [System.IO.Path]::GetFullPath($workRoot)
    $expectedParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
    if ((Split-Path -Parent $resolvedWork) -eq $expectedParent -and
        (Split-Path -Leaf $resolvedWork) -like 'phlosion-scene-fixture-harness-*' -and
        (Test-Path -LiteralPath $resolvedWork)) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
