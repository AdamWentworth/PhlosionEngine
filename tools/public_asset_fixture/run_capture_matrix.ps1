# Public asset fixture capture matrix runner.
#
# Captures four deterministic cases on each native API for both the standalone
# runtime host and the real Phlosion Editor project plugin, verifies the
# process/capture/parity evidence, and validates the images into a
# machine-readable matrix plus a contact sheet.
#
# Requires Windows PowerShell 5.1+ or PowerShell 7+ (both are exercised by
# tests/public_asset_fixture/test_fixture_harness.ps1).

[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$BuildDirectory = 'build-presentation',
    [string]$Configuration = 'Debug',
    [string]$ArtifactsRoot = 'artifacts/m1-d',
    [string]$RunId = (Get-Date -Format 'yyyyMMdd-HHmmss'),
    [string[]]$Apis = @('opengl', 'vulkan', 'd3d12'),
    [string[]]$Hosts = @('runtime', 'editor'),
    [int]$RuntimeScreenshotFrame = 6,
    [int]$EditorScreenshotFrame = 8,
    [int]$EditorFrames = 14,
    [int]$ProcessTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
Import-Module (Join-Path $PSScriptRoot 'FixtureProcess.psm1') -Force

$allowedApis = @('opengl', 'vulkan', 'd3d12')
$allowedHosts = @('runtime', 'editor')
$approvedThresholds = @{
    mae = 0.01
    rmse = 0.08
    changed_ratio = 0.05
    channel_tolerance = 8
}

if ($Apis.Count -eq 0) {
    throw 'At least one native API is required.'
}
if ($Hosts.Count -eq 0) {
    throw 'At least one host is required.'
}
if (($Apis | Select-Object -Unique).Count -ne $Apis.Count) {
    throw 'API arguments must be unique.'
}
if (($Hosts | Select-Object -Unique).Count -ne $Hosts.Count) {
    throw 'Host arguments must be unique.'
}
foreach ($api in $Apis) {
    if ($allowedApis -notcontains $api) {
        throw "Unsupported native API: $api"
    }
}
foreach ($hostName in $Hosts) {
    if ($allowedHosts -notcontains $hostName) {
        throw "Unsupported fixture host: $hostName"
    }
}
if ($RuntimeScreenshotFrame -lt 3) {
    throw 'RuntimeScreenshotFrame must be at least 3 so the replacement case completes before capture.'
}
if ($EditorScreenshotFrame -lt 4) {
    throw 'EditorScreenshotFrame must be at least 4 so the replacement case completes before capture.'
}
if ($EditorFrames -le $EditorScreenshotFrame) {
    throw 'EditorFrames must exceed EditorScreenshotFrame.'
}
if ($ProcessTimeoutSeconds -lt 5) {
    throw 'ProcessTimeoutSeconds must be at least 5.'
}
if ($RunId -notmatch '^[A-Za-z0-9._-]+$') {
    throw "RunId contains unsupported characters: $RunId"
}
if ($Configuration -notmatch '^[A-Za-z0-9._-]+$') {
    throw "Configuration contains unsupported characters: $Configuration"
}

$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$buildOutput = Join-Path $engineRootPath (Join-Path $BuildDirectory $Configuration)
$runtimeExe = Join-Path $buildOutput 'PublicAssetFixtureRuntime.exe'
$editorExe = Join-Path $buildOutput 'PhlosionEditor.exe'
$guardExe = Join-Path $buildOutput 'PublicAssetFixtureGuard.exe'
$pluginDll = Join-Path $buildOutput 'PublicAssetFixtureEditorPlugin.dll'

foreach ($required in @($runtimeExe, $editorExe, $guardExe, $pluginDll)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required fixture artifact is missing: $required"
    }
}

$cases = @(
    @{ Name = 'a-frame0'; Frame = 0 },
    @{ Name = 'a-frame30'; Frame = 30 },
    @{ Name = 'failed-replacement'; Frame = 30 },
    @{ Name = 'valid-replacement-b'; Frame = 30 }
)

$artifactsRootPath = if ([System.IO.Path]::IsPathRooted($ArtifactsRoot)) {
    $ArtifactsRoot
} else {
    Join-Path $engineRootPath $ArtifactsRoot
}
$runDirectory = Join-Path $artifactsRootPath $RunId
if (Test-Path -LiteralPath $runDirectory) {
    throw "Fixture run directory already exists; refusing to reuse stale captures: $runDirectory"
}
New-Item -ItemType Directory -Path $runDirectory | Out-Null

$descriptorPath = $null
if ($Hosts -contains 'editor') {
    $projectDirectory = Join-Path $runDirectory 'project'
    $pluginDirectory = Join-Path $projectDirectory (Join-Path '.phlosion/editor' $Configuration)
    $contentSceneDirectory = Join-Path $projectDirectory 'content/scenes'
    New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $contentSceneDirectory -Force | Out-Null
    Copy-Item -LiteralPath $pluginDll `
        -Destination (Join-Path $pluginDirectory 'PublicAssetFixtureEditorPlugin.dll')

    $descriptorPath = Join-Path $projectDirectory 'phlosion.project.json'
    $descriptor = [ordered]@{
        schema_version = 2
        project_id = 'phlosion-public-asset-fixture'
        display_name = 'Phlosion Public Asset Fixture'
        content_mounts = @(
            [ordered]@{
                id = 'fixture'
                root = 'content'
                required = $true
            }
        )
        startup_scene = [ordered]@{ scene_id = 'fixture/scene' }
        environments = @(
            [ordered]@{
                asset_id = 'fixture/environment'
                display_name = 'Synthetic Fixture Environment'
                kind = 'cooked'
                mount = 'fixture'
                path = 'scenes/fixture.phscene'
            }
        )
        scenes = @(
            [ordered]@{
                scene_id = 'fixture/scene'
                display_name = 'Public Asset Fixture'
                category = 'Fixture'
                environment_asset_id = 'fixture/environment'
                status = 'implemented'
            }
        )
        editor_plugin = [ordered]@{
            library = 'PublicAssetFixtureEditorPlugin'
            directory = '.phlosion/editor/{config}'
        }
    }
    [System.IO.File]::WriteAllText(
        $descriptorPath,
        ($descriptor | ConvertTo-Json -Depth 8),
        [System.Text.UTF8Encoding]::new($false))
}

function Get-FixtureFileIdentity([string]$Path) {
    $item = Get-Item -LiteralPath $Path
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    return [ordered]@{
        path = $item.FullName
        sha256 = $hash
        bytes = [uint64]$item.Length
        last_write_utc = $item.LastWriteTimeUtc.ToString('o')
    }
}

function Get-FixtureBackendDisplayName([string]$Api) {
    switch ($Api) {
        'opengl' { return 'OpenGL' }
        'vulkan' { return 'Vulkan' }
        'd3d12' { return 'D3D12' }
        default { return $Api }
    }
}

$sourceRevision = (& git -C $engineRootPath rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceRevision -notmatch '^[0-9a-fA-F]{7,40}$') {
    throw 'Could not resolve the engine source revision.'
}
$sourceStatus = @(& git -C $engineRootPath status --porcelain)
$sourceDirty = $sourceStatus.Count -gt 0

$binaries = [ordered]@{
    runtime = Get-FixtureFileIdentity $runtimeExe
    editor = Get-FixtureFileIdentity $editorExe
    plugin = Get-FixtureFileIdentity $pluginDll
    guard = Get-FixtureFileIdentity $guardExe
}

$entries = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]

foreach ($hostName in $Hosts) {
    foreach ($api in $Apis) {
        foreach ($case in $cases) {
            $caseName = $case.Name
            $caseFrame = [int]$case.Frame
            $caseDirectory = Join-Path $runDirectory (Join-Path $hostName (Join-Path $api $caseName))
            New-Item -ItemType Directory -Path $caseDirectory -Force | Out-Null
            $capturePath = Join-Path $caseDirectory 'capture.png'
            $handshakePath = Join-Path $caseDirectory 'handshake.json'
            $stdoutPath = Join-Path $caseDirectory 'stdout.log'
            $stderrPath = Join-Path $caseDirectory 'stderr.log'
            $metricsPath = Join-Path $caseDirectory 'metrics.json'
            $stateDirectory = Join-Path $caseDirectory 'state'
            $label = "$hostName/$api/$caseName"

            $screenshotFrame = if ($hostName -eq 'runtime') {
                $RuntimeScreenshotFrame
            } else {
                $EditorScreenshotFrame
            }
            $environmentOverrides = @{
                PHLOSION_FIXTURE_API = $api
                PHLOSION_FIXTURE_CASE = $caseName
                PHLOSION_FIXTURE_LOGICAL_FRAME = "$caseFrame"
                PHLOSION_FIXTURE_HANDSHAKE = $handshakePath
                PHLOSION_BACKEND_SCREENSHOT_PATH = $capturePath
                PHLOSION_BACKEND_SCREENSHOT_FRAME = "$screenshotFrame"
                PHLOSION_BACKEND_SCREENSHOT_DEFER = '0'
                PHLOSION_PARITY_CONTRACT_FATAL = '1'
            }

            $runStarted = (Get-Date).ToUniversalTime()
            if ($hostName -eq 'runtime') {
                $arguments = @(
                    "--api=$api",
                    "--case=$caseName",
                    "--handshake=$handshakePath",
                    "--screenshot-frame=$screenshotFrame")
                $processResult = Invoke-FixtureProcess `
                    -FilePath $runtimeExe `
                    -Arguments $arguments `
                    -WorkingDirectory $engineRootPath `
                    -EnvironmentOverrides $environmentOverrides `
                    -TimeoutSeconds $ProcessTimeoutSeconds
            } else {
                New-Item -ItemType Directory -Path $stateDirectory -Force | Out-Null
                $arguments = @(
                    "--project=$descriptorPath",
                    "--renderer=$api",
                    '--hidden',
                    "--state-directory=$stateDirectory",
                    '--fixed-delta=0.016666667',
                    "--frames=$EditorFrames",
                    "--metrics-output=$metricsPath")
                $processResult = Invoke-FixtureProcess `
                    -FilePath $editorExe `
                    -Arguments $arguments `
                    -WorkingDirectory $engineRootPath `
                    -EnvironmentOverrides $environmentOverrides `
                    -TimeoutSeconds $ProcessTimeoutSeconds
            }

            [System.IO.File]::WriteAllText($stdoutPath, $processResult.Stdout)
            [System.IO.File]::WriteAllText($stderrPath, $processResult.Stderr)

            if ($processResult.TimedOut) {
                $failures.Add("$label timed out after $ProcessTimeoutSeconds seconds")
            } elseif ($processResult.ExitCode -ne 0) {
                $failures.Add("$label exited with code $($processResult.ExitCode)")
            }

            $screenshotBackend = ''
            $observedFrame = -1
            $screenshotWidth = 0
            $screenshotHeight = 0
            if (-not (Test-Path -LiteralPath $capturePath)) {
                $failures.Add("$label produced no screenshot")
            } else {
                $captureItem = Get-Item -LiteralPath $capturePath
                if ($captureItem.LastWriteTimeUtc -lt $runStarted) {
                    $failures.Add("$label screenshot is stale")
                }
                $pngSize = Get-FixturePngSize $capturePath
                $screenshotWidth = $pngSize.Width
                $screenshotHeight = $pngSize.Height
            }
            if (-not (Test-Path -LiteralPath $handshakePath)) {
                $failures.Add("$label produced no handshake")
            }

            $combined = $processResult.Stdout + "`n" + $processResult.Stderr
            # '@(...)' keeps a zero/one-record result an array on Windows
            # PowerShell 5.1, where a bare scalar has no usable .Count.
            $records = @(Get-FixtureScreenshotRecords $combined)
            $display = Get-FixtureBackendDisplayName $api
            $failedRecords = @($records | Where-Object { $_.Status -eq 'FAILED' })
            $wroteRecords = @($records | Where-Object {
                $_.Status -eq 'WROTE' -and $_.Backend -eq $display
            })
            if ($failedRecords.Count -gt 0) {
                $failures.Add("$label reported a screenshot failure record")
            }
            if ($records.Count -ne 1 -or $wroteRecords.Count -ne 1) {
                $failures.Add("$label did not produce exactly one matching screenshot record")
            } else {
                $record = $wroteRecords[0]
                $screenshotBackend = $record.Backend
                $observedFrame = $record.Frame
                if ($record.Frame -ne $screenshotFrame) {
                    $failures.Add("$label observed capture frame $($record.Frame) does not match the requested $screenshotFrame")
                }
                if ([System.IO.Path]::GetFullPath($record.Path) -ne [System.IO.Path]::GetFullPath($capturePath)) {
                    $failures.Add("$label screenshot record path does not match the requested capture path")
                }
                if ($record.Width -ne $screenshotWidth -or $record.Height -ne $screenshotHeight) {
                    $failures.Add("$label screenshot record dimensions do not match the PNG")
                }
            }
            if ($combined -notmatch [regex]::Escape("[ParityContract][$display] PASS")) {
                $failures.Add("$label did not report a passing parity contract for $display")
            }
            if ($combined -match [regex]::Escape("[ParityContract][$display] FAIL")) {
                $failures.Add("$label reported a failing parity contract for $display")
            }
            if ($combined -match 'using OpenGL compatibility mode') {
                $failures.Add("$label reported a renderer fallback to OpenGL")
            }

            if ($hostName -eq 'editor') {
                if (-not (Test-Path -LiteralPath $metricsPath)) {
                    $failures.Add("$label produced no editor metrics")
                } else {
                    try {
                        $metrics = Get-Content -LiteralPath $metricsPath -Raw | ConvertFrom-Json
                        if ($metrics.renderer.backend -ne $api) {
                            $failures.Add("$label editor metrics backend is $($metrics.renderer.backend)")
                        }
                        if ([int]$metrics.capture.frames -lt $EditorFrames) {
                            $failures.Add("$label editor metrics frame count is below the requested frames")
                        }
                    } catch {
                        $failures.Add("$label editor metrics could not be parsed: $($_.Exception.Message)")
                    }
                }
            }

            $entries.Add([ordered]@{
                host = $hostName
                api = $api
                case = $caseName
                png = $capturePath
                handshake = $handshakePath
                stdout = $stdoutPath
                stderr = $stderrPath
                metrics = $metricsPath
                exit_code = $processResult.ExitCode
                requested_capture_frame = $screenshotFrame
                screenshot_backend = $screenshotBackend
                observed_capture_frame = $observedFrame
                screenshot_size = @($screenshotWidth, $screenshotHeight)
            })
            Write-Host "[PublicAssetFixture][Runner] $label exit=$($processResult.ExitCode) frame=$observedFrame"
        }
    }
}

$manifestPath = Join-Path $runDirectory 'manifest.json'
$matrixPath = Join-Path $runDirectory 'matrix.json'
$sheetPath = Join-Path $runDirectory 'contact-sheet.png'
$manifest = [ordered]@{
    run_id = $RunId
    engine_root = $engineRootPath
    build_directory = $BuildDirectory
    configuration = $Configuration
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    powershell_version = $PSVersionTable.PSVersion.ToString()
    dotnet_version = [System.Environment]::Version.ToString()
    source_revision = $sourceRevision
    source_dirty = $sourceDirty
    source_status = $sourceStatus
    editor_frames = $EditorFrames
    capture_frames = [ordered]@{
        runtime = $RuntimeScreenshotFrame
        editor = $EditorScreenshotFrame
    }
    process_timeout_seconds = $ProcessTimeoutSeconds
    binaries = $binaries
    thresholds = $approvedThresholds
    entries = $entries
}
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 10),
    [System.Text.UTF8Encoding]::new($false))

$guardLog = Join-Path $runDirectory 'guard.log'
$guardResult = Invoke-FixtureProcess `
    -FilePath $guardExe `
    -Arguments @(
        'matrix',
        "--manifest=$manifestPath",
        "--out=$matrixPath",
        "--sheet=$sheetPath",
        '--require-complete') `
    -WorkingDirectory $engineRootPath `
    -TimeoutSeconds 600
[System.IO.File]::WriteAllText(
    $guardLog,
    ($guardResult.Stdout + "`n" + $guardResult.Stderr))
if ($guardResult.TimedOut) {
    $failures.Add('guard matrix timed out')
} elseif ($guardResult.ExitCode -ne 0) {
    $failures.Add("guard matrix exited with code $($guardResult.ExitCode)")
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Error $failure -ErrorAction Continue
    }
    Write-Host "[PublicAssetFixture][Runner] FAILED run=$RunId artifacts=$runDirectory"
    exit 1
}

Write-Host "[PublicAssetFixture][Runner] PASS run=$RunId artifacts=$runDirectory"
Write-Host "[PublicAssetFixture][Runner] matrix=$matrixPath"
Write-Host "[PublicAssetFixture][Runner] sheet=$sheetPath"
exit 0
