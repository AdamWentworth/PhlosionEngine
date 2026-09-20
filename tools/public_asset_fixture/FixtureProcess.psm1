# Shared native-process launcher for the public asset fixture harness.
#
# Windows PowerShell 5.1 / .NET Framework and PowerShell 7 / .NET Core are both
# supported. Process.Kill(true) is used for tree cleanup when the runtime
# provides it; the fallback is taskkill /T /F for .NET Framework.

Set-StrictMode -Version 2.0

function ConvertTo-FixtureCommandLineArgument {
    [CmdletBinding()]
    param([AllowEmptyString()][string]$Value)

    if ($null -eq $Value) {
        return '""'
    }
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $backslashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append('\' * ($backslashes * 2 + 1))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) {
            [void]$builder.Append('\' * $backslashes)
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0) {
        [void]$builder.Append('\' * ($backslashes * 2))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-FixtureProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = (Get-Location).Path,
        [hashtable]$EnvironmentOverrides = @{},
        [int]$TimeoutSeconds = 180
    )

    if ($TimeoutSeconds -lt 1) {
        throw 'TimeoutSeconds must be positive.'
    }
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = (($Arguments | ForEach-Object {
        ConvertTo-FixtureCommandLineArgument $_
    }) -join ' ')
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($name in $EnvironmentOverrides.Keys) {
        $startInfo.EnvironmentVariables[[string]$name] =
            [string]$EnvironmentOverrides[$name]
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start fixture process: $FilePath"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
        $treeKilled = $false
        try {
            $process.Kill($true)
            $treeKilled = $true
        } catch {
            $treeKilled = $false
        }
        if (-not $treeKilled -and -not $process.HasExited) {
            # .NET Framework has no Kill(true); taskkill must see the live
            # parent while enumerating the tree, so it runs before Kill().
            try {
                $taskkill = Join-Path $env:SystemRoot 'System32\taskkill.exe'
                & $taskkill /T /F /PID $process.Id *> $null
                $treeKilled = $true
            } catch {
                $treeKilled = $false
            }
        }
        if (-not $treeKilled -and -not $process.HasExited) {
            try {
                $process.Kill()
            } catch {
            }
        }
        [void]$process.WaitForExit(10000)
    }
    $stdout = ''
    $stderr = ''
    try {
        $stdout = $stdoutTask.Result
    } catch {
    }
    try {
        $stderr = $stderrTask.Result
    } catch {
    }
    $exitCode = if ($timedOut) { -1 } else { $process.ExitCode }
    $process.Dispose()
    return [pscustomobject]@{
        ExitCode = $exitCode
        TimedOut = $timedOut
        Stdout = $stdout
        Stderr = $stderr
        CommandLine = ($FilePath + ' ' + $startInfo.Arguments)
    }
}

function Get-FixtureScreenshotRecords {
    [CmdletBinding()]
    param([AllowEmptyString()][string]$Text)

    $records = New-Object System.Collections.Generic.List[object]
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\[Screenshot\]\[([^\]]+)\]\s+(WROTE|FAILED)\s+(.*?)\s+size=(\d+)x(\d+)\s+frame=(\d+)\s*$') {
            $records.Add([pscustomobject]@{
                Backend = $Matches[1]
                Status = $Matches[2]
                Path = $Matches[3]
                Width = [int]$Matches[4]
                Height = [int]$Matches[5]
                Frame = [int]$Matches[6]
            })
        }
    }
    # The comma keeps the array intact so callers see a zero/one/many-element
    # array instead of a bare scalar on Windows PowerShell 5.1.
    return ,$records.ToArray()
}

function Get-FixturePngSize {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 24) {
        throw "PNG is too small: $Path"
    }
    if ($bytes[0] -ne 0x89 -or $bytes[1] -ne 0x50 -or
        $bytes[2] -ne 0x4e -or $bytes[3] -ne 0x47) {
        throw "File is not a PNG: $Path"
    }
    $width = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor
        ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $height = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor
        ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    return [pscustomobject]@{ Width = $width; Height = $height }
}

Export-ModuleMember -Function Invoke-FixtureProcess,
    ConvertTo-FixtureCommandLineArgument,
    Get-FixtureScreenshotRecords,
    Get-FixturePngSize
