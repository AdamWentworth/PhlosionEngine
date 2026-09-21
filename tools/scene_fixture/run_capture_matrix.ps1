# glTF scene fixture capture matrix runner.
#
# Captures the six authored cases on each native API for both the standalone
# runtime host and the real Phlosion Editor project plugin (36 captures),
# records the process/handshake/hash evidence and validates the images through
# the scene fixture guard.
#
# Requires Windows PowerShell 5.1+ or PowerShell 7+.

[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$BuildDirectory = 'build-presentation',
    [string]$Configuration = 'Debug',
    [string]$ArtifactsRoot = 'artifacts/m1-k/matrix',
    [string]$RunId = (Get-Date -Format 'yyyyMMdd-HHmmss'),
    [string[]]$Apis = @('opengl', 'vulkan', 'd3d12'),
    [string[]]$Hosts = @('runtime', 'editor'),
    [int]$RuntimeScreenshotFrame = 6,
    [int]$EditorScreenshotFrame = 6,
    [int]$EditorFrames = 12,
    [int]$RuntimeWidth = 640,
    [int]$RuntimeHeight = 360,
    [int]$ProcessTimeoutSeconds = 180,
    [string]$ExpectedBaseline = '',
    [switch]$Lifecycle
)

$ErrorActionPreference = 'Stop'
if ($Lifecycle) {
    $RuntimeScreenshotFrame = 42
    $EditorScreenshotFrame = 42
    $EditorFrames = 48
}
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
$processModule = Join-Path $PSScriptRoot '..\public_asset_fixture\FixtureProcess.psm1'
Import-Module (Resolve-Path -LiteralPath $processModule).Path -Force

$allowedApis = @('opengl', 'vulkan', 'd3d12')
$allowedHosts = @('runtime', 'editor')
$approvedThresholds = @{
    normalized_mae = 0.01
    normalized_rmse = 0.08
    changed_ratio = 0.05
    channel_tolerance = 8
}

if ($Apis.Count -eq 0) { throw 'At least one native API is required.' }
if ($Hosts.Count -eq 0) { throw 'At least one host is required.' }
foreach ($api in $Apis) {
    if ($allowedApis -notcontains $api) { throw "Unsupported native API: $api" }
}
foreach ($hostName in $Hosts) {
    if ($allowedHosts -notcontains $hostName) {
        throw "Unsupported fixture host: $hostName"
    }
}
if ($EditorFrames -le $EditorScreenshotFrame) {
    throw 'EditorFrames must exceed EditorScreenshotFrame.'
}
if ($RunId -notmatch '^[A-Za-z0-9._-]+$') {
    throw "RunId contains unsupported characters: $RunId"
}

$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$buildOutput = Join-Path $engineRootPath (Join-Path $BuildDirectory $Configuration)
$runtimeExe = Join-Path $buildOutput 'SceneFixtureRuntime.exe'
$editorExe = Join-Path $buildOutput 'PhlosionEditor.exe'
$guardExe = Join-Path $buildOutput 'SceneFixtureGuard.exe'
$pluginDll = Join-Path $buildOutput 'SceneFixtureEditorPlugin.dll'
foreach ($required in @($runtimeExe, $editorExe, $guardExe, $pluginDll)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required fixture artifact is missing: $required"
    }
}

$cases = @(
    @{ Name = 'a-bind'; Frame = 0 },
    @{ Name = 'a-pose'; Frame = 30 },
    @{ Name = 'failed-replacement'; Frame = 60 },
    @{ Name = 'replacement-b'; Frame = 90 },
    @{ Name = 'emission-off'; Frame = 120 },
    @{ Name = 'unloaded'; Frame = 150 }
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
    New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
    Copy-Item -LiteralPath $pluginDll `
        -Destination (Join-Path $pluginDirectory 'SceneFixtureEditorPlugin.dll')
    $descriptorPath = Join-Path $projectDirectory 'phlosion.project.json'
    $descriptor = [ordered]@{
        schema_version = 2
        project_id = 'phlosion-scene-fixture'
        display_name = 'Phlosion Scene Fixture'
        content_mounts = @(
            [ordered]@{ id = 'fixture'; root = 'content'; required = $true }
        )
        startup_scene = [ordered]@{ scene_id = 'fixture/scene' }
        environments = @(
            [ordered]@{
                asset_id = 'fixture/environment'
                display_name = 'Synthetic Scene Environment'
                kind = 'cooked'
                mount = 'fixture'
                path = 'scenes/fixture.phscene'
            }
        )
        scenes = @(
            [ordered]@{
                scene_id = 'fixture/scene'
                display_name = 'Public Scene Fixture'
                category = 'Fixture'
                environment_asset_id = 'fixture/environment'
                status = 'implemented'
            }
        )
        editor_plugin = [ordered]@{
            library = 'SceneFixtureEditorPlugin'
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
if ([string]::IsNullOrWhiteSpace($ExpectedBaseline)) {
    $expectedBaselineRevision = $sourceRevision
} else {
    $resolved = (& git -C $engineRootPath rev-parse --verify "$ExpectedBaseline^{commit}" | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $resolved -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Could not resolve ExpectedBaseline '$ExpectedBaseline' to a commit."
    }
    if ($resolved -ne $sourceRevision) {
        throw "Engine revision $sourceRevision is not the expected baseline $ExpectedBaseline"
    }
    $expectedBaselineRevision = $resolved
}
$sourceStatus = @(& git -C $engineRootPath status --porcelain)
$sourceDirty = $sourceStatus.Count -gt 0

$binaries = @(
    ([ordered]@{ role = 'runtime' } + (Get-FixtureFileIdentity $runtimeExe)),
    ([ordered]@{ role = 'editor' } + (Get-FixtureFileIdentity $editorExe)),
    ([ordered]@{ role = 'plugin' } + (Get-FixtureFileIdentity $pluginDll)),
    ([ordered]@{ role = 'guard' } + (Get-FixtureFileIdentity $guardExe))
)

$failures = New-Object System.Collections.Generic.List[string]
$captures = New-Object System.Collections.Generic.List[object]

function Invoke-SceneFixtureCapture {
    param(
        [string]$hostName,
        [string]$api,
        [hashtable]$case,
        [int]$CaptureWidth = 0,
        [int]$CaptureHeight = 0
    )
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
        PHLOSION_SCENE_FIXTURE_API = $api
        PHLOSION_SCENE_FIXTURE_CASE = $caseName
        PHLOSION_SCENE_FIXTURE_LOGICAL_FRAME = "$caseFrame"
        PHLOSION_SCENE_FIXTURE_HANDSHAKE = $handshakePath
        PHLOSION_SCENE_FIXTURE_LIFECYCLE = $(if ($Lifecycle) { '1' } else { '0' })
        PHLOSION_BACKEND_SCREENSHOT_PATH = $capturePath
        PHLOSION_BACKEND_SCREENSHOT_FRAME = "$screenshotFrame"
        PHLOSION_BACKEND_SCREENSHOT_DEFER = '0'
        PHLOSION_PARITY_CONTRACT_FATAL = '1'
    }

    $runStarted = (Get-Date).ToUniversalTime()
    if ($hostName -eq 'runtime') {
        $processResult = Invoke-FixtureProcess `
            -FilePath $runtimeExe `
            -Arguments @(
                "--api=$api",
                "--case=$caseName",
                "--handshake=$handshakePath",
                "--screenshot-frame=$screenshotFrame",
                "--width=$CaptureWidth",
                "--height=$CaptureHeight") `
            -WorkingDirectory $engineRootPath `
            -EnvironmentOverrides $environmentOverrides `
            -TimeoutSeconds $ProcessTimeoutSeconds
    } else {
        New-Item -ItemType Directory -Path $stateDirectory -Force | Out-Null
        $processResult = Invoke-FixtureProcess `
            -FilePath $editorExe `
            -Arguments @(
                "--project=$descriptorPath",
                "--renderer=$api",
                '--hidden',
                "--state-directory=$stateDirectory",
                '--fixed-delta=0.016666667',
                "--frames=$EditorFrames",
                "--metrics-output=$metricsPath") `
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

    $viewportWidth = 0
    $viewportHeight = 0
    if (-not (Test-Path -LiteralPath $capturePath)) {
        $failures.Add("$label produced no screenshot")
    } else {
        $captureItem = Get-Item -LiteralPath $capturePath
        if ($captureItem.LastWriteTimeUtc -lt $runStarted) {
            $failures.Add("$label screenshot is stale")
        }
    }
    if (-not (Test-Path -LiteralPath $handshakePath)) {
        $failures.Add("$label produced no handshake")
    } else {
        try {
            $handshake = Get-Content -LiteralPath $handshakePath -Raw | ConvertFrom-Json
            $expectedIdentity = "phlosion-scene-fixture-v1/$caseName"
            if ($handshake.sample_identity -ne $expectedIdentity) {
                $failures.Add("$label handshake sample identity is '$($handshake.sample_identity)'")
            }
            if ($handshake.host -ne $hostName) {
                $failures.Add("$label handshake host is '$($handshake.host)'")
            }
            if ($handshake.logical_frame -ne $caseFrame) {
                $failures.Add("$label handshake logical frame is '$($handshake.logical_frame)'")
            }
            $viewportWidth = [int]$handshake.surface.width
            $viewportHeight = [int]$handshake.surface.height
        } catch {
            $failures.Add("$label handshake could not be parsed: $($_.Exception.Message)")
        }
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
                # The editor capture is the whole window; the plugin handshake
                # records the fixture viewport inside it.
                if ($viewportWidth -gt 0 -and
                    (([int]$metrics.capture.viewport_width -ne $viewportWidth) -or
                     ([int]$metrics.capture.viewport_height -ne $viewportHeight))) {
                    $failures.Add("$label editor viewport does not match its handshake surface")
                }
            } catch {
                $failures.Add("$label editor metrics could not be parsed: $($_.Exception.Message)")
            }
        }
    }

    $pngWidth = 0
    $pngHeight = 0
    $pngHash = ''
    $pngBytes = [uint64]0
    if (Test-Path -LiteralPath $capturePath) {
        $size = Get-FixturePngSize $capturePath
        $pngWidth = [int]$size.Width
        $pngHeight = [int]$size.Height
        $pngHash = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $pngBytes = [uint64](Get-Item -LiteralPath $capturePath).Length
        if ($hostName -eq 'runtime' -and $viewportWidth -gt 0 -and
            ($pngWidth -ne $viewportWidth -or $pngHeight -ne $viewportHeight)) {
            $failures.Add("$label capture dimensions do not match its handshake surface")
        }
    }
    $handshakeHash = ''
    if (Test-Path -LiteralPath $handshakePath) {
        $handshakeHash =
            (Get-FileHash -LiteralPath $handshakePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    $combined = $processResult.Stdout + "`n" + $processResult.Stderr
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
        if ($record.Frame -ne $screenshotFrame) {
            $failures.Add("$label observed capture frame $($record.Frame) does not match the requested $screenshotFrame")
        }
        if ([System.IO.Path]::GetFullPath($record.Path) -ne [System.IO.Path]::GetFullPath($capturePath)) {
            $failures.Add("$label screenshot record path does not match the requested capture path")
        }
        if ($record.Width -ne $pngWidth -or $record.Height -ne $pngHeight) {
            $failures.Add("$label screenshot record dimensions do not match the PNG")
        }
    }
    if ($combined -notmatch [regex]::Escape("[ParityContract][$display] PASS")) {
        $failures.Add("$label did not report a passing parity contract for $display")
    }
    if ($combined -match 'using OpenGL compatibility mode') {
        $failures.Add("$label reported a renderer fallback to OpenGL")
    }

    Write-Host "[SceneFixture][Runner] $label exit=$($processResult.ExitCode)"
    return [ordered]@{
        case = $caseName
        host = $hostName
        api = $api
        logical_frame = $caseFrame
        exit_code = $processResult.ExitCode
        # `surface` is the authored fixture viewport both hosts render (the
        # editor window contains its chrome around it); `window` is the raw
        # captured image.
        surface = [ordered]@{ width = $viewportWidth; height = $viewportHeight }
        window = [ordered]@{ width = $pngWidth; height = $pngHeight }
        handshake_surface = [ordered]@{ width = $viewportWidth; height = $viewportHeight }
        png = $capturePath
        png_sha256 = $pngHash
        png_bytes = $pngBytes
        handshake = $handshakePath
        handshake_sha256 = $handshakeHash
        stdout = $stdoutPath
        stderr = $stderrPath
        metrics = $metricsPath
        requested_capture_frame = $screenshotFrame
    }
}

foreach ($api in $Apis) {
    foreach ($case in $cases) {
        $editorEntry = $null
        $runtimeWidth = $RuntimeWidth
        $runtimeHeight = $RuntimeHeight
        if ($Hosts -contains 'editor') {
            $editorEntry = Invoke-SceneFixtureCapture `
                -hostName 'editor' -api $api -case $case
            $captures.Add($editorEntry)
            if ([int]$editorEntry.handshake_surface.width -le 0 -or
                [int]$editorEntry.handshake_surface.height -le 0) {
                $failures.Add("$api/$($case.Name) editor viewport is not measurable")
            } else {
                $runtimeWidth = [int]$editorEntry.handshake_surface.width
                $runtimeHeight = [int]$editorEntry.handshake_surface.height
            }
        }
        if ($Hosts -contains 'runtime') {
            $runtimeEntry = Invoke-SceneFixtureCapture `
                -hostName 'runtime' -api $api -case $case `
                -CaptureWidth $runtimeWidth -CaptureHeight $runtimeHeight
            $captures.Add($runtimeEntry)
            if ($null -ne $editorEntry -and
                (([int]$runtimeEntry.handshake_surface.width -ne [int]$editorEntry.handshake_surface.width) -or
                 ([int]$runtimeEntry.handshake_surface.height -ne [int]$editorEntry.handshake_surface.height))) {
                $failures.Add("$api/$($case.Name) runtime viewport does not match the editor viewport")
            }
        }
    }
}

$manifestPath = Join-Path $runDirectory 'manifest.json'
$manifest = [ordered]@{
    schema = 'phlosion-scene-fixture-matrix-v1'
    lifecycle = [bool]$Lifecycle
    run_id = $RunId
    engine_root = $engineRootPath
    build_directory = $BuildDirectory
    configuration = $Configuration
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    source_revision = $sourceRevision
    source_baseline = $expectedBaselineRevision
    source_dirty = $sourceDirty
    source_status = $sourceStatus
    editor_frames = $EditorFrames
    capture_frames = [ordered]@{
        runtime = $RuntimeScreenshotFrame
        editor = $EditorScreenshotFrame
    }
    process_timeout_seconds = $ProcessTimeoutSeconds
    thresholds = $approvedThresholds
    binaries = $binaries
    captures = $captures
}
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 10),
    [System.Text.UTF8Encoding]::new($false))

$guardLog = Join-Path $runDirectory 'guard.log'
$guardResult = Invoke-FixtureProcess `
    -FilePath $guardExe `
    -Arguments @('matrix', "--manifest=$manifestPath") `
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
    Write-Host "[SceneFixture][Runner] FAILED run=$RunId artifacts=$runDirectory"
    exit 1
}

Write-Host "[SceneFixture][Runner] PASS run=$RunId artifacts=$runDirectory"
Write-Host "[SceneFixture][Runner] manifest=$manifestPath"
Write-Host "[SceneFixture][Runner] sheet=$(Join-Path $runDirectory 'contact-sheet.png')"
exit 0
