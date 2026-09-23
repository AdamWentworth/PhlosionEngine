# Negative and adversarial tests for the animation fixture harness.
#
# Runs without a GPU: synthetic captures and handshakes come from the guard's
# hermetic `synth` command, and process handling is exercised through the guard
# `echo` helper plus a harmless two-level PowerShell child. Paths deliberately
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
# The diagnostic work directory is retained after the run and its name is
# reserved without -Force, so a collision fails and a fresh GUID is tried.
$workRoot = $null
for ($attempt = 0; $attempt -lt 8 -and $null -eq $workRoot; ++$attempt) {
    $candidate = Join-Path ([System.IO.Path]::GetTempPath()) (
        'phlosion animation fixture harness [x] & y ' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $candidate -ErrorAction Stop | Out-Null
        $workRoot = $candidate
    } catch {
        Write-Host "[AnimationFixture][Harness] temp directory collision, retrying: $candidate"
    }
}
if ($null -eq $workRoot) {
    throw 'could not acquire a unique animation fixture harness directory'
}

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
    @{ name = 'bind'; frame = 0 },
    @{ name = 'linear-intermediate'; frame = 30 },
    @{ name = 'linear-near-endpoint'; frame = 60 },
    @{ name = 'step-state'; frame = 90 },
    @{ name = 'cubic-midpoint'; frame = 120 },
    @{ name = 'cubic-early'; frame = 150 },
    @{ name = 'crossfade-quarter'; frame = 180 },
    @{ name = 'state-crossfade'; frame = 210 }
)
$captureFrames = @{ runtime = 6; editor = 8 }
$approvedThresholds = [ordered]@{
    mae = 0.01
    rmse = 0.08
    changed_ratio = 0.05
    channel_tolerance = 8
}
$revision = 'afaeb1ec470434fe3da10f7c5f3c8238b2fc5068'

$guardItem = Get-Item -LiteralPath $guardPath
$binaryIdentity = [ordered]@{
    path = $guardItem.FullName
    sha256 = (Get-FileHash -LiteralPath $guardPath -Algorithm SHA256).Hash.ToLowerInvariant()
    bytes = [uint64]$guardItem.Length
}

$entries = New-Object System.Collections.Generic.List[object]
foreach ($hostName in $hosts) {
    foreach ($api in $apis) {
        foreach ($case in $cases) {
            $casePng = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.png')))
            $caseHandshake = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.json')))
            $caseStdout = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.out.log')))
            $caseStderr = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.err.log')))
            $caseMetrics = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.metrics.json')))
            New-Item -ItemType Directory -Path (Split-Path -Parent $casePng) -Force |
                Out-Null
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
                    "--out-png=$casePng",
                    "--out-handshake=$caseHandshake",
                    "--host=$hostName",
                    "--api=$api",
                    "--case=$($case.name)",
                    '--defect=none',
                    '--surface=320x180') `
                -TimeoutSeconds 60
            Check-Fixture ($synth.ExitCode -eq 0) "synth $hostName/$api/$($case.name) failed: $($synth.Stderr)"
            [System.IO.File]::WriteAllText(
                $caseStdout,
                "[Screenshot][$display] WROTE $casePng size=320x180 frame=$screenshotFrame`n" +
                "[ParityContract][$display] PASS signature=0000000000000000`n")
            [System.IO.File]::WriteAllText($caseStderr, '')
            if ($hostName -eq 'editor') {
                Write-FixtureJson $caseMetrics ([ordered]@{
                    schema = 'phlosion-editor-baseline-metrics-v1'
                    capture = [ordered]@{
                        frames = 14
                        width = 320
                        height = 180
                        viewport_width = 320
                        viewport_height = 180
                    }
                    renderer = [ordered]@{ backend = $api }
                })
            }
            $entries.Add([ordered]@{
                host = $hostName
                api = $api
                case = $case.name
                png = $casePng
                handshake = $caseHandshake
                stdout = $caseStdout
                stderr = $caseStderr
                metrics = $caseMetrics
                exit_code = 0
                requested_capture_frame = $screenshotFrame
                screenshot_backend = $display
                observed_capture_frame = $screenshotFrame
                screenshot_size = @(320, 180)
                logical_frame = $case.frame
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
    source_revision = $revision
    source_baseline = $revision
    source_dirty = $true
    source_status = @(' M harness')
    editor_frames = 14
    capture_frames = [ordered]@{ runtime = $captureFrames.runtime; editor = $captureFrames.editor }
    process_timeout_seconds = 30
    binaries = [ordered]@{
        runtime = $binaryIdentity
        editor = $binaryIdentity
        plugin = $binaryIdentity
        guard = $binaryIdentity
    }
    thresholds = $approvedThresholds
    entries = $entries
}
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
    [System.IO.File]::WriteAllText($logPath, ($result.Stdout + "`n" + $result.Stderr))
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
                foreach ($failure in $entry.failures) { $text += "`n$failure" }
            }
            foreach ($pair in $matrix.pairwise) {
                foreach ($failure in $pair.failures) { $text += "`n$failure" }
            }
            foreach ($cross in $matrix.cross_case) {
                foreach ($failure in $cross.failures) { $text += "`n$failure" }
            }
        } catch {
        }
    }
    return $text
}

function Copy-And-MutateManifest([string]$Name, [scriptblock]$Mutation) {
    $document = Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
    & $Mutation $document
    $path = Join-Path $workRoot "$Name-manifest.json"
    Write-FixtureJson $path $document
    return $path
}

# Positive coverage: the hermetic 48-entry Cartesian matrix must pass.
Invoke-GuardMatrix -ManifestPath $baseManifestPath -Name 'positive' -ExpectSuccess $true | Out-Null

$duplicatePath = Copy-And-MutateManifest 'duplicate' {
    param($document)
    $document.entries = @($document.entries[0]) * 48
}
$duplicateResult = Invoke-GuardMatrix -ManifestPath $duplicatePath -Name 'duplicate' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $duplicateResult) -match 'duplicate') `
    'duplicate manifest did not report a duplicate tuple'

$missingPath = Copy-And-MutateManifest 'missing' {
    param($document)
    $document.entries = @($document.entries[0..46])
}
$missingResult = Invoke-GuardMatrix -ManifestPath $missingPath -Name 'missing' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingResult) -match '48|missing') `
    'incomplete manifest did not report the missing tuple'

$unknownHostPath = Copy-And-MutateManifest 'unknown-host' {
    param($document)
    $document.entries[0].host = 'bogus'
}
$unknownHostResult = Invoke-GuardMatrix -ManifestPath $unknownHostPath -Name 'unknown-host' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $unknownHostResult) -match 'host is unknown') `
    'unknown host was not reported'

$unknownApiPath = Copy-And-MutateManifest 'unknown-api' {
    param($document)
    $document.entries[0].api = 'metal'
}
$unknownApiResult = Invoke-GuardMatrix -ManifestPath $unknownApiPath -Name 'unknown-api' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $unknownApiResult) -match 'API is unknown') `
    'unknown API was not reported'

$looseThresholdPath = Copy-And-MutateManifest 'loose-threshold' {
    param($document)
    $document.thresholds.mae = 0.5
}
$looseThresholdResult = Invoke-GuardMatrix -ManifestPath $looseThresholdPath -Name 'loose-threshold' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $looseThresholdResult) -match 'looser') `
    'loosened threshold was not reported'

$negativeThresholdPath = Copy-And-MutateManifest 'negative-threshold' {
    param($document)
    $document.thresholds.channel_tolerance = -1
}
$negativeThresholdResult = Invoke-GuardMatrix -ManifestPath $negativeThresholdPath -Name 'negative-threshold' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $negativeThresholdResult) -match 'tolerance') `
    'negative channel tolerance was not reported'

$baselinePath = Copy-And-MutateManifest 'baseline' {
    param($document)
    $document.source_baseline = '0000000000000000000000000000000000000000'
}
$baselineResult = Invoke-GuardMatrix -ManifestPath $baselinePath -Name 'baseline' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $baselineResult) -match 'baseline') `
    'source baseline mismatch was not reported'

$hashPath = Copy-And-MutateManifest 'binary-hash' {
    param($document)
    $document.binaries.guard.sha256 = '0' * 64
}
$hashResult = Invoke-GuardMatrix -ManifestPath $hashPath -Name 'binary-hash' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $hashResult) -match 'hash') `
    'binary hash mismatch was not reported'

# This harness leaves its diagnostic work directory in place on purpose so a
# failed run can be inspected; only the files it mutates are restored in place.
$posedEntry = $baseManifest.entries | Where-Object { $_.case -eq 'linear-intermediate' } |
    Select-Object -First 1
$stepEntry = $baseManifest.entries | Where-Object { $_.case -eq 'step-state' } |
    Select-Object -First 1
$bindEntry = $baseManifest.entries | Where-Object { $_.case -eq 'bind' } |
    Select-Object -First 1
$cubicMidpointEntry = $baseManifest.entries |
    Where-Object { $_.case -eq 'cubic-midpoint' } | Select-Object -First 1
$cubicEarlyEntry = $baseManifest.entries |
    Where-Object { $_.case -eq 'cubic-early' } | Select-Object -First 1
$pristine = @{}
foreach ($entry in @(
        $posedEntry, $stepEntry, $bindEntry, $cubicMidpointEntry, $cubicEarlyEntry)) {
    $pristine[$entry.handshake] = [System.IO.File]::ReadAllBytes($entry.handshake)
    $pristine[$entry.png] = [System.IO.File]::ReadAllBytes($entry.png)
    $pristine[$entry.stdout] = [System.IO.File]::ReadAllBytes($entry.stdout)
}
function Restore-FixtureFile([string]$Path) {
    if ($pristine.ContainsKey($Path)) {
        [System.IO.File]::WriteAllBytes($Path, $pristine[$Path])
    }
}
function Set-FixtureHandshake([string]$Path, [scriptblock]$Mutation) {
    $document = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    & $Mutation $document
    Write-FixtureJson $Path $document
}

# Per-entry provenance: exit code, capture frame, backend and content class.
$exitCodePath = Copy-And-MutateManifest 'exit-code' {
    param($document)
    $document.entries[0].exit_code = 7
}
$exitCodeResult = Invoke-GuardMatrix -ManifestPath $exitCodePath -Name 'exit-code' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $exitCodeResult) -match 'exit code') `
    'nonzero recorded exit code was not reported'

# Missing binary evidence must fail outright instead of skipping validation.
$missingBinariesPath = Copy-And-MutateManifest 'missing-binaries' {
    param($document)
    $document.PSObject.Properties.Remove('binaries')
}
$missingBinariesResult = Invoke-GuardMatrix -ManifestPath $missingBinariesPath -Name 'missing-binaries' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingBinariesResult) -match 'binaries') `
    'a manifest without the binaries object was not reported'

$missingBinaryPath = Copy-And-MutateManifest 'missing-binary' {
    param($document)
    $document.binaries.PSObject.Properties.Remove('plugin')
}
$missingBinaryResult = Invoke-GuardMatrix -ManifestPath $missingBinaryPath -Name 'missing-binary' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingBinaryResult) -match 'binary identity') `
    'a manifest without one binary identity was not reported'

# An entry must request the manifest capture frame for its host.
$entryFramePath = Copy-And-MutateManifest 'entry-frame' {
    param($document)
    $document.entries[0].requested_capture_frame = 9
}
$entryFrameResult = Invoke-GuardMatrix -ManifestPath $entryFramePath -Name 'entry-frame' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $entryFrameResult) -match 'capture frame') `
    'an entry frame that disagrees with the manifest capture frame was not reported'

# A capture frame that disagrees with the raw screenshot log must be rejected.
$frameMismatchPath = Copy-And-MutateManifest 'frame-mismatch' {
    param($document)
}
$frameText = [System.IO.File]::ReadAllText($posedEntry.stdout)
$frameText = $frameText -replace 'frame=6', 'frame=5'
[System.IO.File]::WriteAllText($posedEntry.stdout, $frameText)
$frameMismatchResult = Invoke-GuardMatrix -ManifestPath $frameMismatchPath -Name 'frame-mismatch' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $frameMismatchResult) -match 'capture frame') `
    'capture frame mismatch was not reported'
Restore-FixtureFile $posedEntry.stdout

$backendMismatchPath = Copy-And-MutateManifest 'backend-mismatch' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.actual_backend = 'vulkan'
}
$backendMismatchResult = Invoke-GuardMatrix -ManifestPath $backendMismatchPath -Name 'backend-mismatch' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $backendMismatchResult) -match 'actual backend|requested') `
    'actual backend mismatch was not reported'
Restore-FixtureFile $posedEntry.handshake

$hostPath = Copy-And-MutateManifest 'host-mismatch' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.host = 'editor'
}
$hostResult = Invoke-GuardMatrix -ManifestPath $hostPath -Name 'host-mismatch' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $hostResult) -match 'host') `
    'handshake host mismatch was not reported'
Restore-FixtureFile $posedEntry.handshake

$missingBackendPath = Copy-And-MutateManifest 'missing-requested-backend' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.PSObject.Properties.Remove('requested_backend')
}
$missingBackendResult = Invoke-GuardMatrix -ManifestPath $missingBackendPath -Name 'missing-requested-backend' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingBackendResult) -match 'requested backend') `
    'a missing requested backend was not reported'
Restore-FixtureFile $posedEntry.handshake

$logicalFramePath = Copy-And-MutateManifest 'logical-frame' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.animation.logical_frame = 99
}
$logicalFrameResult = Invoke-GuardMatrix -ManifestPath $logicalFramePath -Name 'logical-frame' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $logicalFrameResult) -match 'logical frame') `
    'handshake logical frame mismatch was not reported'
Restore-FixtureFile $posedEntry.handshake

$clipIndexPath = Copy-And-MutateManifest 'clip-index' {
    param($document)
}
Set-FixtureHandshake $stepEntry.handshake {
    param($handshake)
    $handshake.animation.clip_index = 0
}
$clipIndexResult = Invoke-GuardMatrix -ManifestPath $clipIndexPath -Name 'clip-index' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $clipIndexResult) -match 'clip index') `
    'handshake clip index mismatch was not reported'
Restore-FixtureFile $stepEntry.handshake

$vertexZPath = Copy-And-MutateManifest 'vertex-z' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.vertices[0].position[2] = 0.5
}
$vertexZResult = Invoke-GuardMatrix -ManifestPath $vertexZPath -Name 'vertex-z' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $vertexZResult) -match 'vertex') `
    'a submitted vertex Z mismatch was not reported'
Restore-FixtureFile $posedEntry.handshake

$palettePath = Copy-And-MutateManifest 'palette-space' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.palette[0] = [double]$handshake.palette[0] * 2.0
}
$paletteResult = Invoke-GuardMatrix -ManifestPath $palettePath -Name 'palette-space' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $paletteResult) -match 'palette') `
    'palette space mismatch was not reported'
Restore-FixtureFile $posedEntry.handshake

# The submitted palette allocation must not be swapped between the bind and the
# requested draw.
$paletteAddressPath = Copy-And-MutateManifest 'palette-address' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.presentation.pose_draw_palette_address = '0xdead'
}
$paletteAddressResult = Invoke-GuardMatrix -ManifestPath $paletteAddressPath -Name 'palette-address' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $paletteAddressResult) -match 'palette storage changed') `
    'a swapped palette allocation was not reported'
Restore-FixtureFile $posedEntry.handshake

# A requested palette that still holds the bind values must fail.
$paletteStaysBindPath = Copy-And-MutateManifest 'palette-stays-bind' {
    param($document)
}
Set-FixtureHandshake $posedEntry.handshake {
    param($handshake)
    $handshake.palette = $handshake.bind_palette
}
$paletteStaysBindResult = Invoke-GuardMatrix -ManifestPath $paletteStaysBindPath -Name 'palette-stays-bind' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $paletteStaysBindResult) -match 'palette') `
    'a requested palette that stayed at bind values was not reported'
Restore-FixtureFile $posedEntry.handshake

$blankPath = Copy-And-MutateManifest 'blank' {
    param($document)
}
$blankSynth = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @(
        'synth',
        "--out-png=$($posedEntry.png)",
        "--out-handshake=$($posedEntry.handshake)",
        '--host=runtime',
        '--api=opengl',
        '--case=linear-intermediate',
        '--defect=blank',
        '--surface=320x180') `
    -TimeoutSeconds 60
Check-Fixture ($blankSynth.ExitCode -eq 0) "blank synth failed: $($blankSynth.Stderr)"
$blankResult = Invoke-GuardMatrix -ManifestPath $blankPath -Name 'blank' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $blankResult) -match 'rig_coverage|green') `
    'blank capture was not reported'
Restore-FixtureFile $posedEntry.png
Restore-FixtureFile $posedEntry.handshake

# Disabled skinning still applies the nonidentity mesh transform to raw bind
# vertices, so it must be rejected even on the bind case.
$disabledSkinningPath = Copy-And-MutateManifest 'disabled-skinning-bind' {
    param($document)
}
$disabledSkinningSynth = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @(
        'synth',
        "--out-png=$($bindEntry.png)",
        "--out-handshake=$($bindEntry.handshake)",
        '--host=runtime',
        '--api=opengl',
        '--case=bind',
        '--defect=disabled-skinning',
        '--surface=320x180') `
    -TimeoutSeconds 60
Check-Fixture ($disabledSkinningSynth.ExitCode -eq 0) "disabled-skinning synth failed: $($disabledSkinningSynth.Stderr)"
$disabledSkinningResult = Invoke-GuardMatrix -ManifestPath $disabledSkinningPath -Name 'disabled-skinning-bind' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $disabledSkinningResult) -match 'landmark|rig_coverage') `
    'disabled skinning on the bind case was not reported'
Restore-FixtureFile $bindEntry.png
Restore-FixtureFile $bindEntry.handshake

# A cubic clip evaluated as a LINEAR curve between its keys must be rejected on
# both cubic cases. The LINEAR and STEP cases author the same pose as their
# fallback, so this defect is deliberately only distinguishable there.
foreach ($cubicEntry in @($cubicMidpointEntry, $cubicEarlyEntry)) {
    $cubicCaseName = $cubicEntry.case
    # Each affected entry is restored inside its own iteration, so the second
    # negative cannot pass because the first one left a corrupted capture behind.
    try {
        $linearizedPath = Copy-And-MutateManifest "linearized-$cubicCaseName" {
            param($document)
        }
        $linearizedSynth = Invoke-FixtureProcess `
            -FilePath $guardPath `
            -Arguments @(
                'synth',
                "--out-png=$($cubicEntry.png)",
                "--out-handshake=$($cubicEntry.handshake)",
                '--host=runtime',
                '--api=opengl',
                "--case=$cubicCaseName",
                '--defect=linearized-cubic',
                '--surface=320x180') `
            -TimeoutSeconds 60
        Check-Fixture ($linearizedSynth.ExitCode -eq 0) `
            "linearized-cubic synth for $cubicCaseName failed: $($linearizedSynth.Stderr)"
        $linearizedResult = Invoke-GuardMatrix `
            -ManifestPath $linearizedPath `
            -Name "linearized-$cubicCaseName" `
            -ExpectSuccess $false
        Check-Fixture ((Get-GuardFailureText $linearizedResult) -match 'landmark|rig_coverage') `
            "linearized cubic on $cubicCaseName was not reported"
    } finally {
        Restore-FixtureFile $cubicEntry.png
        Restore-FixtureFile $cubicEntry.handshake
    }
}

# After the targeted mutations are restored, the pristine 48-entry matrix must
# pass again; otherwise a negative could have been caused by stale state.
$pristineAfterLinearized = Invoke-GuardMatrix `
    -ManifestPath $baseManifestPath `
    -Name 'pristine-after-linearized' `
    -ExpectSuccess $true
Check-Fixture ($pristineAfterLinearized.ExitCode -eq 0) `
    'the pristine matrix did not pass after restoring the cubic captures'

# Native process helper: quoting, exit codes and bounded tree cleanup.
$quoteText = 'spaces & percent % caret ^ quote " backslash \ comma, paren( ) bracket[ ]'
$echoResult = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', "--text=$quoteText") `
    -TimeoutSeconds 30
Check-Fixture ($echoResult.ExitCode -eq 0) 'echo helper did not exit zero'
Check-Fixture ($echoResult.Stdout.Contains('[AnimationFixture][Echo] ' + $quoteText)) `
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

$sentinelName = 'PHLOSION_ANIMATION_FIXTURE_HARNESS_SENTINEL'
[Environment]::SetEnvironmentVariable($sentinelName, 'original', 'Process')
Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', '--text=env') `
    -EnvironmentOverrides @{ PHLOSION_ANIMATION_FIXTURE_CASE = 'child-only' } `
    -TimeoutSeconds 30 | Out-Null
Check-Fixture (
    ([Environment]::GetEnvironmentVariable($sentinelName, 'Process')) -eq 'original') `
    'process launcher mutated the caller environment'
[Environment]::SetEnvironmentVariable($sentinelName, $null, 'Process')

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host "[AnimationFixture][Harness] FAIL $failure"
    }
    Write-Host "[AnimationFixture][Harness] FAILED checks=$($failures.Count) work=$workRoot"
    exit 1
}
Write-Host "[AnimationFixture][Harness] PASS work=$workRoot"
exit 0
