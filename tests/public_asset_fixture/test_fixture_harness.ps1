# Negative and adversarial tests for the public fixture harness.
#
# Runs without a GPU: synthetic captures come from the guard's hermetic
# `synth` command, and the process tests use the guard's `echo` helper plus a
# harmless two-level PowerShell child for tree cleanup. Paths deliberately
# contain spaces and shell metacharacters.

[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$ProcessModule = '',
    [Parameter(Mandatory = $true)][string]$GuardExe
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
if ([string]::IsNullOrWhiteSpace($ProcessModule)) {
    $ProcessModule = Join-Path $PSScriptRoot '..\..\tools\public_asset_fixture\FixtureProcess.psm1'
}
Import-Module (Resolve-Path -LiteralPath $ProcessModule).Path -Force

$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$guardPath = (Resolve-Path -LiteralPath $GuardExe).Path
$workRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'phlosion fixture harness [x] & y ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
function Check-Fixture([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        $failures.Add($Message)
    }
}

function Write-FixtureJson([string]$Path, $Document) {
    [System.IO.File]::WriteAllText(
        $Path,
        ($Document | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
}

$hosts = @('runtime', 'editor')
$apis = @('opengl', 'vulkan', 'd3d12')
$cases = @(
    @{ name = 'a-frame0'; frame = 0 },
    @{ name = 'a-frame30'; frame = 30 },
    @{ name = 'failed-replacement'; frame = 30 },
    @{ name = 'valid-replacement-b'; frame = 30 }
)
$captureFrames = @{ runtime = 6; editor = 8 }
$editorFrames = 14
$approvedThresholds = [ordered]@{
    mae = 0.01
    rmse = 0.08
    changed_ratio = 0.05
    channel_tolerance = 8
}

$guardItem = Get-Item -LiteralPath $guardPath
$guardHash = (Get-FileHash -LiteralPath $guardPath -Algorithm SHA256).Hash.ToLowerInvariant()
$binaryIdentity = [ordered]@{
    path = $guardItem.FullName
    sha256 = $guardHash
    bytes = [uint64]$guardItem.Length
}
$binaries = [ordered]@{
    runtime = $binaryIdentity
    editor = $binaryIdentity
    plugin = $binaryIdentity
    guard = $binaryIdentity
}

$entries = New-Object System.Collections.Generic.List[object]
foreach ($hostName in $hosts) {
    foreach ($api in $apis) {
        foreach ($case in $cases) {
            $caseName = $case.name
            $caseFrame = [int]$case.frame
            $caseDirectory = Join-Path $workRoot (Join-Path $hostName (Join-Path $api $caseName))
            New-Item -ItemType Directory -Path $caseDirectory -Force | Out-Null
            $pngPath = Join-Path $caseDirectory 'capture.png'
            $handshakePath = Join-Path $caseDirectory 'handshake.json'
            $stdoutPath = Join-Path $caseDirectory 'stdout.log'
            $stderrPath = Join-Path $caseDirectory 'stderr.log'
            $metricsPath = Join-Path $caseDirectory 'metrics.json'
            $screenshotFrame = $captureFrames[$hostName]
            $display = switch ($api) {
                'opengl' { 'OpenGL' }
                'vulkan' { 'Vulkan' }
                'd3d12' { 'D3D12' }
            }
            $synth = Invoke-FixtureProcess `
                -FilePath $guardPath `
                -Arguments @(
                    'synth',
                    "--out-png=$pngPath",
                    "--out-handshake=$handshakePath",
                    "--host=$hostName",
                    "--api=$api",
                    "--case=$caseName",
                    '--surface=320x180') `
                -TimeoutSeconds 60
            Check-Fixture ($synth.ExitCode -eq 0) "synth $hostName/$api/$caseName failed: $($synth.Stderr)"
            [System.IO.File]::WriteAllText(
                $stdoutPath,
                "[Screenshot][$display] WROTE $pngPath size=320x180 frame=$screenshotFrame`n" +
                "[ParityContract][$display] PASS signature=0000000000000000`n")
            [System.IO.File]::WriteAllText($stderrPath, '')
            if ($hostName -eq 'editor') {
                $metrics = [ordered]@{
                    schema = 'phlosion-editor-baseline-metrics-v1'
                    capture = [ordered]@{
                        frames = $editorFrames
                        width = 320
                        height = 180
                        viewport_width = 320
                        viewport_height = 180
                    }
                    renderer = [ordered]@{ backend = $api }
                }
                Write-FixtureJson $metricsPath $metrics
            }
            $entries.Add([ordered]@{
                host = $hostName
                api = $api
                case = $caseName
                png = $pngPath
                handshake = $handshakePath
                stdout = $stdoutPath
                stderr = $stderrPath
                metrics = $metricsPath
                exit_code = 0
                requested_capture_frame = $screenshotFrame
                screenshot_backend = $display
                observed_capture_frame = $screenshotFrame
                screenshot_size = @(320, 180)
                logical_frame = $caseFrame
            })
        }
    }
}

$baseManifest = [ordered]@{
    run_id = 'harness'
    engine_root = $engineRootPath
    build_directory = 'harness'
    configuration = 'Debug'
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    powershell_version = $PSVersionTable.PSVersion.ToString()
    dotnet_version = [System.Environment]::Version.ToString()
    source_revision = '0123456789abcdef0123456789abcdef01234567'
    source_dirty = $true
    source_status = @(' M harness')
    editor_frames = $editorFrames
    capture_frames = [ordered]@{ runtime = $captureFrames.runtime; editor = $captureFrames.editor }
    process_timeout_seconds = 30
    binaries = $binaries
    thresholds = $approvedThresholds
    entries = $entries
}
# Runner evidence parsing helpers must survive zero/one/many records on
# Windows PowerShell 5.1 and parse paths that contain spaces and brackets.
$sampleLog = "[Screenshot][OpenGL] WROTE C:\temp dir\a [x] & b\capture.png size=320x180 frame=6`n" +
    "[ParityContract][OpenGL] PASS`n"
$parsedRecords = Get-FixtureScreenshotRecords $sampleLog
Check-Fixture (@($parsedRecords).Count -eq 1) 'screenshot parser did not return exactly one record'
Check-Fixture (
    $parsedRecords[0].Backend -eq 'OpenGL' -and
    $parsedRecords[0].Frame -eq 6 -and
    $parsedRecords[0].Width -eq 320 -and
    $parsedRecords[0].Height -eq 180) `
    'screenshot parser fields are wrong'
$emptyRecords = Get-FixtureScreenshotRecords ''
Check-Fixture (@($emptyRecords).Count -eq 0) 'screenshot parser did not return an empty array'
$failedSample = "[Screenshot][Vulkan] FAILED C:\x.png size=1x1 frame=0`n"
$failedRecords = Get-FixtureScreenshotRecords $failedSample
Check-Fixture (
    @($failedRecords).Count -eq 1 -and $failedRecords[0].Status -eq 'FAILED') `
    'screenshot parser dropped the FAILED record'
$pngInfo = Get-FixturePngSize $entries[0].png
Check-Fixture (
    $pngInfo.Width -eq 320 -and $pngInfo.Height -eq 180) `
    'PNG size helper returned wrong dimensions'

$baseManifestPath = Join-Path $workRoot 'manifest-base.json'
Write-FixtureJson $baseManifestPath $baseManifest

function Invoke-GuardMatrix(
    [string]$ManifestPath,
    [string]$Name,
    [bool]$ExpectSuccess) {
    $outPath = Join-Path $workRoot "$Name-matrix.json"
    $sheetPath = Join-Path $workRoot "$Name-sheet.png"
    $logPath = Join-Path $workRoot "$Name-guard.log"
    $result = Invoke-FixtureProcess `
        -FilePath $guardPath `
        -Arguments @(
            'matrix',
            "--manifest=$ManifestPath",
            "--out=$outPath",
            "--sheet=$sheetPath",
            '--require-complete') `
        -TimeoutSeconds 300
    [System.IO.File]::WriteAllText(
        $logPath,
        ($result.Stdout + "`n" + $result.Stderr))
    if ($ExpectSuccess) {
        Check-Fixture ($result.ExitCode -eq 0) "guard matrix '$Name' should have passed: $($result.Stderr)$($result.Stdout)"
    } else {
        Check-Fixture ($result.ExitCode -ne 0) "guard matrix '$Name' should have failed"
    }
    $result | Add-Member -NotePropertyName MatrixPath -NotePropertyValue $outPath
    return $result
}

function Get-GuardFailureText($Result) {
    $text = $Result.Stdout + "`n" + $Result.Stderr
    if ($Result.MatrixPath -and (Test-Path -LiteralPath $Result.MatrixPath)) {
        try {
            $matrix = Get-Content -LiteralPath $Result.MatrixPath -Raw | ConvertFrom-Json
            foreach ($entry in $matrix.entries) {
                foreach ($failure in $entry.failures) {
                    $text += "`n$failure"
                }
            }
            foreach ($pair in $matrix.pairwise) {
                foreach ($failure in $pair.failures) {
                    $text += "`n$failure"
                }
            }
            foreach ($cross in $matrix.cross_case) {
                foreach ($failure in $cross.failures) {
                    $text += "`n$failure"
                }
            }
        } catch {
        }
    }
    return $text
}

function Copy-And-MutateManifest(
    [string]$Name,
    [scriptblock]$Mutation) {
    $document = Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
    & $Mutation $document
    $path = Join-Path $workRoot "$Name-manifest.json"
    Write-FixtureJson $path $document
    return $path
}

# Positive CLI coverage: the hermetic 24-entry Cartesian matrix must pass.
Invoke-GuardMatrix -ManifestPath $baseManifestPath -Name 'positive' -ExpectSuccess $true | Out-Null

# Duplicate tuple, missing tuple, unknown host/API and loosened thresholds.
$duplicatePath = Copy-And-MutateManifest 'duplicate' {
    param($document)
    $document.entries = @($document.entries[0]) * 24
}
$duplicateResult = Invoke-GuardMatrix -ManifestPath $duplicatePath -Name 'duplicate' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $duplicateResult) -match 'duplicate') `
    'duplicate manifest did not report a duplicate tuple'

$missingPath = Copy-And-MutateManifest 'missing' {
    param($document)
    $document.entries = @($document.entries[0..22])
}
$missingResult = Invoke-GuardMatrix -ManifestPath $missingPath -Name 'missing' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $missingResult) -match '24|missing') `
    'incomplete manifest did not report the missing tuple'

$unknownHostPath = Copy-And-MutateManifest 'unknown-host' {
    param($document)
    $document.entries[0].host = 'bogus'
}
$unknownHostResult = Invoke-GuardMatrix -ManifestPath $unknownHostPath -Name 'unknown-host' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $unknownHostResult) -match 'host is unknown') `
    'unknown host was not reported'

$unknownApiPath = Copy-And-MutateManifest 'unknown-api' {
    param($document)
    $document.entries[0].api = 'metal'
}
$unknownApiResult = Invoke-GuardMatrix -ManifestPath $unknownApiPath -Name 'unknown-api' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $unknownApiResult) -match 'API is unknown') `
    'unknown API was not reported'

$looseThresholdPath = Copy-And-MutateManifest 'loose-threshold' {
    param($document)
    $document.thresholds.mae = 0.5
}
$looseThresholdResult = Invoke-GuardMatrix -ManifestPath $looseThresholdPath -Name 'loose-threshold' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $looseThresholdResult) -match 'looser') `
    'loosened threshold was not reported'

$negativeThresholdPath = Copy-And-MutateManifest 'negative-threshold' {
    param($document)
    $document.thresholds.channel_tolerance = -1
}
$negativeThresholdResult = Invoke-GuardMatrix -ManifestPath $negativeThresholdPath -Name 'negative-threshold' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $negativeThresholdResult) -match 'tolerance') `
    'negative channel tolerance was not reported'

$invalidJsonPath = Join-Path $workRoot 'invalid-json-manifest.json'
$invalidJson = Get-Content -LiteralPath $baseManifestPath -Raw
$invalidJson = $invalidJson -replace '"mae"\s*:\s*0\.01', '"mae": NaN'
[System.IO.File]::WriteAllText($invalidJsonPath, $invalidJson)
$invalidJsonResult = Invoke-GuardMatrix -ManifestPath $invalidJsonPath -Name 'invalid-json' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $invalidJsonResult) -match 'JSON parse failed') `
    'non-finite JSON threshold was not reported'

# Per-entry provenance: exit code, capture frame, backend and blank content.
$exitCodePath = Copy-And-MutateManifest 'exit-code' {
    param($document)
    $document.entries[0].exit_code = 7
}
$exitCodeResult = Invoke-GuardMatrix -ManifestPath $exitCodePath -Name 'exit-code' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $exitCodeResult) -match 'exit code') `
    'nonzero recorded exit code was not reported'

$frameMismatchPath = Copy-And-MutateManifest 'frame-mismatch' {
    param($document)
}
$frameLog = $baseManifest.entries[0].stdout
$frameText = [System.IO.File]::ReadAllText($frameLog)
$frameText = $frameText -replace 'frame=6', 'frame=5'
[System.IO.File]::WriteAllText($frameLog, $frameText)
$frameMismatchResult = Invoke-GuardMatrix -ManifestPath $frameMismatchPath -Name 'frame-mismatch' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $frameMismatchResult) -match 'capture frame') `
    'capture frame mismatch was not reported'

$backendMismatchPath = Copy-And-MutateManifest 'backend-mismatch' {
    param($document)
}
$backendHandshake = Get-Content -LiteralPath $baseManifest.entries[0].handshake -Raw | ConvertFrom-Json
$backendHandshake.actual_backend = 'vulkan'
Write-FixtureJson $baseManifest.entries[0].handshake $backendHandshake
$backendMismatchResult = Invoke-GuardMatrix -ManifestPath $backendMismatchPath -Name 'backend-mismatch' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $backendMismatchResult) -match 'actual backend') `
    'actual backend mismatch was not reported'

$logicalFramePath = Copy-And-MutateManifest 'logical-frame' {
    param($document)
}
$logicalHandshake = Get-Content -LiteralPath $baseManifest.entries[1].handshake -Raw | ConvertFrom-Json
$logicalHandshake.logical_frame = 99
Write-FixtureJson $baseManifest.entries[1].handshake $logicalHandshake
$logicalFrameResult = Invoke-GuardMatrix -ManifestPath $logicalFramePath -Name 'logical-frame' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $logicalFrameResult) -match 'logical frame') `
    'handshake logical frame mismatch was not reported'

$blankPath = Copy-And-MutateManifest 'blank' {
    param($document)
}
$blankSynth = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @(
        'synth',
        "--out-png=$($baseManifest.entries[0].png)",
        "--out-handshake=$($baseManifest.entries[0].handshake)",
        '--host=runtime',
        '--api=opengl',
        '--case=a-frame0',
        '--surface=320x180',
        '--blank') `
    -TimeoutSeconds 60
Check-Fixture ($blankSynth.ExitCode -eq 0) "blank synth failed: $($blankSynth.Stderr)"
$blankResult = Invoke-GuardMatrix -ManifestPath $blankPath -Name 'blank' -ExpectSuccess $false
Check-Fixture (
    (Get-GuardFailureText $blankResult) -match 'viewport|green') `
    'blank capture was not reported'

# Native process helper: quoting, exit codes and bounded tree cleanup.
$quoteText = 'spaces & percent % caret ^ quote " backslash \ comma, paren( ) bracket[ ]'
$echoResult = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', "--text=$quoteText") `
    -TimeoutSeconds 30
Check-Fixture ($echoResult.ExitCode -eq 0) 'echo helper did not exit zero'
Check-Fixture (
    $echoResult.Stdout.Contains('[PublicAssetFixture][Echo] ' + $quoteText)) `
    'quoted argument was not delivered verbatim'

$exitResult = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', '--text=exit', '--exit=7') `
    -TimeoutSeconds 30
Check-Fixture ($exitResult.ExitCode -eq 7) 'nonzero child exit code was not preserved'

$grandchildPath = Join-Path $workRoot 'grandchild.ps1'
$childPath = Join-Path $workRoot 'child.ps1'
$markerPath = Join-Path $workRoot 'marker.txt'
$grandchildScript = @'
param([string]$Marker)
Start-Sleep -Seconds 4
Set-Content -LiteralPath $Marker -Value alive
'@
$childScript = @'
param([string]$Marker,[string]$Grandchild)
$quotedGrandchild = '"' + $Grandchild + '"'
$quotedMarker = '"' + $Marker + '"'
Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$quotedGrandchild,'-Marker',$quotedMarker) -WindowStyle Hidden | Out-Null
Start-Sleep -Seconds 60
'@
[System.IO.File]::WriteAllText($grandchildPath, $grandchildScript)
[System.IO.File]::WriteAllText($childPath, $childScript)
$timeoutResult = Invoke-FixtureProcess `
    -FilePath 'powershell.exe' `
    -Arguments @(
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        $childPath,
        '-Marker',
        $markerPath,
        '-Grandchild',
        $grandchildPath) `
    -TimeoutSeconds 2
Check-Fixture ($timeoutResult.TimedOut) 'timeout test did not report a timeout'
Start-Sleep -Seconds 7
Check-Fixture (-not (Test-Path -LiteralPath $markerPath)) 'timed-out process tree was not cleaned up'
$lingering = @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" |
    Where-Object { $_.CommandLine -like "*$grandchildPath*" })
Check-Fixture ($lingering.Count -eq 0) 'grandchild process survived the timeout cleanup'

# The launcher must not mutate the caller's environment.
$sentinelName = 'PHLOSION_FIXTURE_HARNESS_SENTINEL'
[Environment]::SetEnvironmentVariable($sentinelName, 'original', 'Process')
Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', '--text=env') `
    -EnvironmentOverrides @{ PHLOSION_FIXTURE_CASE = 'child-only' } `
    -TimeoutSeconds 30 | Out-Null
Check-Fixture (
    ([Environment]::GetEnvironmentVariable($sentinelName, 'Process')) -eq 'original') `
    'process launcher mutated the caller environment'
[Environment]::SetEnvironmentVariable($sentinelName, $null, 'Process')

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host "[PublicAssetFixture][Harness] FAIL $failure"
    }
    Write-Host "[PublicAssetFixture][Harness] FAILED checks=$($failures.Count) work=$workRoot"
    exit 1
}
Write-Host "[PublicAssetFixture][Harness] PASS work=$workRoot"
exit 0
