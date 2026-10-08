param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("Gui", "Cli", "Web", "Server", "None")]
    [string]$Client = "Gui",

    [switch]$NoTests,
    [switch]$NoRun,
    [switch]$UseExternalVcpkg,
    [switch]$KeepDependencies
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$NativeRoot = Join-Path $Root "projects\native"
$WebRoot = Join-Path $Root "projects\web"
Set-Location $Root

$VcpkgTag = "2026.07.29"
$VcpkgBaseline = "9e593bb18ea69cc5095e012465dcd675a822ed0d"

function Require-Command([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "$Name was not found on PATH." }
    return $cmd.Source
}

function ConvertTo-WindowsProcessArgument {
    param([AllowEmptyString()][string]$Argument)

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    # Follow the CommandLineToArgvW / MSVC argv quoting rules:
    # - quote arguments containing whitespace or quotes;
    # - double backslashes that precede a quote;
    # - double trailing backslashes before the closing quote.
    $escaped = [regex]::Replace($Argument, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Join-WindowsProcessArguments {
    param([string[]]$ArgumentList = @())

    if (-not $ArgumentList -or $ArgumentList.Count -eq 0) {
        return ''
    }

    return (($ArgumentList | ForEach-Object { ConvertTo-WindowsProcessArgument ([string]$_) }) -join ' ')
}

function Invoke-NativeProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string]$WorkingDirectory = $Root,
        [string]$Label = "native process",
        [int]$TimeoutSeconds = 1800,
        [int]$HeartbeatSeconds = 15
    )

    # Never pipe native stderr through Windows PowerShell 5.1. Start the child
    # directly on this console, then poll it ourselves. This preserves ordinary
    # Git/MSBuild stderr output without turning it into PowerShell ErrorRecords,
    # and gives long-running build steps visible liveness + a hard upper bound.
    $argumentString = Join-WindowsProcessArguments $ArgumentList

    $startArgs = @{
        FilePath = $FilePath
        WorkingDirectory = $WorkingDirectory
        NoNewWindow = $true
        PassThru = $true
    }
    if (-not [string]::IsNullOrEmpty($argumentString)) {
        $startArgs['ArgumentList'] = $argumentString
    }

    $process = Start-Process @startArgs
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $nextHeartbeat = $HeartbeatSeconds

    while ($true) {
        $process.Refresh()
        if ($process.HasExited) { break }

        if ($TimeoutSeconds -gt 0 -and $stopwatch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            Write-Host "$Label exceeded ${TimeoutSeconds}s; terminating PID $($process.Id)..." -ForegroundColor Red
            try {
                Start-Process -FilePath "taskkill.exe" -ArgumentList "/PID $($process.Id) /T /F" -NoNewWindow -Wait | Out-Null
            } catch {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
            throw "$Label timed out after ${TimeoutSeconds}s."
        }

        if ($HeartbeatSeconds -gt 0 -and $stopwatch.Elapsed.TotalSeconds -ge $nextHeartbeat) {
            $elapsed = [int][Math]::Floor($stopwatch.Elapsed.TotalSeconds)
            Write-Host "[$Label] still running (${elapsed}s)..." -ForegroundColor DarkGray
            $nextHeartbeat += $HeartbeatSeconds
        }

        Start-Sleep -Milliseconds 250
    }

    # Ensure the native process handle has observed final termination before
    # reading ExitCode. On Windows PowerShell 5.1, merely observing HasExited
    # can race process-handle state and has previously allowed failed MSBuild
    # invocations to look successful.
    $process.WaitForExit()
    $process.Refresh()
    $stopwatch.Stop()
    return [int]$process.ExitCode
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$Description,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string]$WorkingDirectory = $Root
    )

    $exitCode = Invoke-NativeProcess -FilePath $FilePath -ArgumentList $ArgumentList -WorkingDirectory $WorkingDirectory -Label $Description
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode."
    }
}

function Invoke-NativeDiagnostic {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string]$WorkingDirectory = $Root
    )

    return (Invoke-NativeProcess -FilePath $FilePath -ArgumentList $ArgumentList -WorkingDirectory $WorkingDirectory -Label "diagnostic" -TimeoutSeconds 60 -HeartbeatSeconds 0)
}

function Test-VcpkgBaseline([string]$Path) {
    if (-not (Test-Path (Join-Path $Path ".git"))) { return $false }
    if (-not (Test-Path (Join-Path $Path "versions\baseline.json"))) { return $false }

    $savedPreference = $ErrorActionPreference
    $exitCode = $null
    try {
        $ErrorActionPreference = 'SilentlyContinue'
        & git -C $Path cat-file -e "${VcpkgBaseline}:versions/baseline.json" 1>$null 2>$null
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $savedPreference }
    return ($exitCode -eq 0)
}

function Initialize-PinnedVcpkg {
    $Path = Join-Path $Root ".deps\vcpkg"
    $Deps = Split-Path $Path

    if ((Test-Path $Path) -and -not (Test-VcpkgBaseline $Path)) {
        Write-Host "Repairing invalid project-local vcpkg checkout: $Path" -ForegroundColor Yellow
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    if (-not (Test-Path (Join-Path $Path ".git"))) {
        New-Item -ItemType Directory -Force -Path $Deps | Out-Null
        Invoke-NativeChecked -Description "vcpkg repository init" -FilePath "git" -ArgumentList @(
            "init", "--quiet", $Path)
        Invoke-NativeChecked -Description "vcpkg origin setup" -FilePath "git" -ArgumentList @(
            "-C", $Path, "remote", "add", "origin", "https://github.com/microsoft/vcpkg.git")
    }

    if (-not (Test-VcpkgBaseline $Path)) {
        Invoke-NativeChecked -Description "vcpkg baseline fetch" -FilePath "git" -ArgumentList @(
            "-C", $Path, "fetch", "--force", "--depth", "1", "origin", $VcpkgBaseline)
        Invoke-NativeChecked -Description "vcpkg baseline checkout" -FilePath "git" -ArgumentList @(
            "-C", $Path, "checkout", "--quiet", "--detach", $VcpkgBaseline)
    }

    if (-not (Test-VcpkgBaseline $Path)) {
        throw "Pinned vcpkg checkout does not contain versions/baseline.json at $VcpkgBaseline."
    }

    # Run the bootstrap script by filename from inside the vcpkg checkout.
    # This deliberately keeps the absolute checkout path out of cmd.exe's
    # command line, avoiding the nested /c quoting rules that break when the
    # project path contains spaces (or when Start-Process adds another quoting
    # layer). cmd.exe resolves bootstrap-vcpkg.bat relative to WorkingDirectory.
    $bootstrap = Join-Path $Path "bootstrap-vcpkg.bat"
    if (-not (Test-Path -LiteralPath $bootstrap)) {
        throw "Pinned vcpkg checkout is missing bootstrap-vcpkg.bat: $bootstrap"
    }

    $commandProcessor = if ($env:ComSpec) { $env:ComSpec } else { "cmd.exe" }
    Invoke-NativeChecked -Description "vcpkg bootstrap" -FilePath $commandProcessor `
        -ArgumentList @("/d", "/c", "bootstrap-vcpkg.bat", "-disableMetrics") `
        -WorkingDirectory $Path
    return $Path
}

function Resolve-VcpkgRoot {
    if ($UseExternalVcpkg) {
        if (-not $env:VCPKG_ROOT) { throw "-UseExternalVcpkg was specified, but VCPKG_ROOT is not set." }
        $external = (Resolve-Path $env:VCPKG_ROOT).Path
        if (-not (Test-VcpkgBaseline $external)) {
            throw "External VCPKG_ROOT '$external' cannot resolve pinned baseline $VcpkgBaseline."
        }
        return $external
    }
    return (Initialize-PinnedVcpkg)
}

function Get-CMakeVersion([string]$CMake) {
    $line = & $CMake --version | Select-Object -First 1
    if ($line -notmatch '(\d+)\.(\d+)\.(\d+)') { throw "Unable to determine CMake version from '$line'." }
    return [version]::new([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
}

function Find-VsWhere {
    $candidates = @()
    if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe" }
    if ($env:ProgramFiles) { $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe" }
    $candidates = @($candidates | Where-Object { Test-Path $_ })
    if ($candidates.Count -gt 0) { return $candidates[0] }
    $cmd = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Find-VisualStudio([string]$VsWhere, [string]$VersionRange) {
    if (-not $VsWhere) { return $null }
    $path = & $VsWhere -latest -products * -version $VersionRange -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0) { return $null }
    $value = $path | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($value)) { return $null }
    return $value.Trim()
}

function Select-VisualStudioPreset([version]$CMakeVersion) {
    $vswhere = Find-VsWhere
    $vs2026 = Find-VisualStudio $vswhere '[18.0,19.0)'
    $vs2022 = Find-VisualStudio $vswhere '[17.0,18.0)'

    if ($vs2026 -and $CMakeVersion -ge [version]'4.2.0') {
        return [pscustomobject]@{ Preset = 'windows-vs2026'; Version = '2026'; Path = $vs2026 }
    }
    if ($vs2022) {
        return [pscustomobject]@{ Preset = 'windows-vs2022'; Version = '2022'; Path = $vs2022 }
    }
    if ($vs2026 -and $CMakeVersion -lt [version]'4.2.0') {
        throw "Visual Studio 2026 is installed, but CMake $CMakeVersion cannot generate VS 2026 projects. Install CMake 4.2+."
    }
    throw "No Visual Studio 2022/2026 installation with the Desktop C++ toolchain was found."
}

function Wait-EngCoderHealth([string]$Url, [int]$TimeoutSeconds = 12) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri "$Url/health" -TimeoutSec 1
            if ($response.StatusCode -eq 200) { return $true }
        }
        catch {}
        Start-Sleep -Milliseconds 150
    }
    return $false
}

function Stop-StaleProjectBuildProcesses([string]$BuildDirectory) {
    # Do not kill arbitrary compiler/build processes on the machine. Only stop
    # processes whose command line points at this EngCoder source/build tree.
    $needles = @(
        $BuildDirectory.ToLowerInvariant(),
        $NativeRoot.ToLowerInvariant()
    )
    $names = @('MSBuild.exe', 'cmake.exe', 'ctest.exe', 'cl.exe', 'link.exe')

    $matches = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        if (-not $_.CommandLine -or ($names -notcontains $_.Name)) { return $false }
        $commandLine = $_.CommandLine.ToLowerInvariant()
        foreach ($needle in $needles) {
            if ($commandLine.Contains($needle)) { return $true }
        }
        # Fresh source extractions have versioned parent directories. Clean up
        # stale EngCoder build workers from older extracted versions too,
        # while still refusing to touch unrelated MSBuild/CMake jobs.
        if ($commandLine -match 'engcoder-[^\\/\s]+.*[\\/]out[\\/]build') { return $true }
        return $false
    })

    foreach ($entry in $matches) {
        if ($entry.ProcessId -eq $PID) { continue }
        Write-Host "Stopping stale project build process $($entry.Name) (PID $($entry.ProcessId))..." -ForegroundColor Yellow
        try {
            Start-Process -FilePath 'taskkill.exe' -ArgumentList "/PID $($entry.ProcessId) /T /F" -NoNewWindow -Wait | Out-Null
        }
        catch {
            Stop-Process -Id $entry.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
}

function Remove-TransientDependencies {
    if ($UseExternalVcpkg) {
        Write-Host 'External VCPKG_ROOT was requested; dependency checkout cleanup is skipped.' -ForegroundColor DarkGray
        return
    }
    if ($KeepDependencies) {
        Write-Host 'Keeping project-local dependency checkout (-KeepDependencies).' -ForegroundColor DarkGray
        return
    }

    $localVcpkg = Join-Path $Root '.deps\vcpkg'
    if (Test-Path -LiteralPath $localVcpkg) {
        Write-Host 'Removing transient project-local vcpkg checkout/cache...' -ForegroundColor Cyan
        try {
            Remove-Item -LiteralPath $localVcpkg -Recurse -Force -ErrorAction Stop
        }
        catch {
            Write-Host "PowerShell cleanup failed; falling back to rmdir: $($_.Exception.Message)" -ForegroundColor Yellow
            $cmd = if ($env:ComSpec) { $env:ComSpec } else { 'cmd.exe' }
            $cleanupCode = Invoke-NativeProcess -FilePath $cmd `
                -ArgumentList @('/d', '/c', 'rmdir', '/s', '/q', $localVcpkg) `
                -WorkingDirectory $Root -Label 'dependency cleanup' -TimeoutSeconds 120 -HeartbeatSeconds 10
            if ($cleanupCode -ne 0 -and (Test-Path -LiteralPath $localVcpkg)) {
                throw "Dependency cleanup failed with exit code ${cleanupCode}: $localVcpkg"
            }
        }
    }

    $depsRoot = Join-Path $Root '.deps'
    if (Test-Path -LiteralPath $depsRoot) {
        $remaining = @(Get-ChildItem -LiteralPath $depsRoot -Force -ErrorAction SilentlyContinue)
        if ($remaining.Count -eq 0) {
            Remove-Item -LiteralPath $depsRoot -Force -ErrorAction SilentlyContinue
        }
    }

    Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
    Write-Host 'Transient dependency checkout removed.' -ForegroundColor Green
    Write-Host 'Keeping out/build/.../vcpkg_installed because the generated Visual Studio solution still uses it.' -ForegroundColor DarkGray
}

function Stop-PreviousStandaloneServer([string]$ServerPath) {
    $runDirectory = Join-Path $Root ".engcoder\run"
    $pidFile = Join-Path $runDirectory "engcoder_server.pid"
    $pathFile = Join-Path $runDirectory "engcoder_server.path"
    if (-not ((Test-Path $pidFile) -and (Test-Path $pathFile))) { return }

    $previousPath = (Get-Content $pathFile -Raw).Trim()
    $previousPidText = (Get-Content $pidFile -Raw).Trim()
    $previousPid = 0
    if ([int]::TryParse($previousPidText, [ref]$previousPid)) {
        $previousProcess = Get-Process -Id $previousPid -ErrorAction SilentlyContinue
        if ($previousProcess -and ($previousPath -eq $ServerPath -or $previousPath -match '(?:EngCoder|EngCoderAgent)-.*(?:EngCoderServer|engcoder_server)\.exe$')) {
            Write-Host "Stopping previous standalone EngCoder server (PID $previousPid)..." -ForegroundColor Yellow
            Stop-Process -Id $previousPid -Force -ErrorAction SilentlyContinue
            try { Wait-Process -Id $previousPid -Timeout 3 -ErrorAction SilentlyContinue } catch {}
        }
    }
    Remove-Item $pidFile, $pathFile -ErrorAction SilentlyContinue
}

function Start-StandaloneServer([string]$Server, [string]$Cli, [string]$RunDirectory) {
    $pidFile = Join-Path $RunDirectory "engcoder_server.pid"
    $pathFile = Join-Path $RunDirectory "engcoder_server.path"
    $stdoutLog = Join-Path $RunDirectory "engcoder_server.stdout.log"
    $stderrLog = Join-Path $RunDirectory "engcoder_server.stderr.log"

    if ($env:ENGCODER_SERVER_PORT) {
        $runtimePort = [int]$env:ENGCODER_SERVER_PORT
    } else {
        $runtimePort = 14321
        $env:ENGCODER_SERVER_PORT = [string]$runtimePort
    }
    $runtimeUrl = "http://127.0.0.1:$runtimePort"
    $uiUrl = "$runtimeUrl/ui/"
    $env:ENGCODER_AGENT_URL = $runtimeUrl

    Remove-Item $stdoutLog, $stderrLog -ErrorAction SilentlyContinue
    Write-Host "Launching standalone EngCoder server..." -ForegroundColor Cyan
    $process = Start-Process `
        -FilePath $Server `
        -WorkingDirectory $Root `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -PassThru

    Set-Content -Path $pidFile -Value $process.Id -NoNewline
    Set-Content -Path $pathFile -Value $Server -NoNewline

    if (-not (Wait-EngCoderHealth $runtimeUrl)) {
        $process.Refresh()
        if ($process.HasExited) {
            $details = if (Test-Path $stderrLog) { Get-Content $stderrLog -Raw } else { "No stderr log." }
            throw "EngCoderServer exited with code $($process.ExitCode).`n$details"
        }
        throw "EngCoderServer did not become healthy at $runtimeUrl. See $stderrLog"
    }

    Write-Host "Standalone server ready (PID $($process.Id))." -ForegroundColor Green
    return [pscustomobject]@{ Url = $runtimeUrl; UiUrl = $uiUrl; Pid = $process.Id }
}

function Start-EngCoderSelectedClient([string]$BuildDirectory, [string]$ConfigurationName) {
    $binaryDirectory = Join-Path $BuildDirectory $ConfigurationName
    $server = Join-Path $binaryDirectory "EngCoderServer.exe"
    $cli = Join-Path $binaryDirectory "EngCoderCLI.exe"
    $gui = Join-Path $binaryDirectory "EngCoder.exe"
    foreach ($required in @($server, $cli, $gui)) {
        if (-not (Test-Path $required)) { throw "Built application was not found: $required" }
    }

    $runDirectory = Join-Path $Root ".engcoder\run"
    $guiStdoutLog = Join-Path $runDirectory "engcoder_gui.stdout.log"
    $guiStderrLog = Join-Path $runDirectory "engcoder_gui.stderr.log"
    New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

    $env:ENGCODER_WORKSPACE = $Root
    $env:ENGCODER_WEB_ROOT = $WebRoot

    # The native GUI/CLI embed the runtime now. Any old standalone server from
    # an earlier auto.bat run is stale for those modes and must not be left around.
    Stop-PreviousStandaloneServer -ServerPath $server

    if ($Client -in @("Cli", "Web", "Server")) {
        Write-Host "Running model-backend doctor..." -ForegroundColor Cyan
        $doctorExitCode = Invoke-NativeDiagnostic -FilePath $cli -ArgumentList @("--doctor") -WorkingDirectory $Root
        if ($doctorExitCode -ne 0) {
            Write-Host "Backend doctor exited with code $doctorExitCode. Client launch will continue." -ForegroundColor Yellow
        }
    } elseif ($Client -eq "Gui") {
        Write-Host "GUI mode: EngCoder owns its embedded runtime; skipping the standalone pre-launch doctor." -ForegroundColor DarkGray
    }

    switch ($Client) {
        "Gui" {
            Write-Host "Launching EngCoder desktop with shared Desktop/Web UI and embedded runtime..." -ForegroundColor Cyan
            Remove-Item $guiStdoutLog, $guiStderrLog -ErrorAction SilentlyContinue
            $guiProcess = Start-Process `
                -FilePath $gui `
                -WorkingDirectory $Root `
                -RedirectStandardOutput $guiStdoutLog `
                -RedirectStandardError $guiStderrLog `
                -PassThru
            for ($attempt = 0; $attempt -lt 12; $attempt++) {
                Start-Sleep -Milliseconds 250
                $guiProcess.Refresh()
                if ($guiProcess.HasExited) {
                    $details = if (Test-Path $guiStderrLog) { Get-Content $guiStderrLog -Raw } else { "No GUI stderr log." }
                    throw "EngCoder exited during startup with code $($guiProcess.ExitCode).`n$details`nGUI logs: $guiStdoutLog ; $guiStderrLog"
                }
            }
            Write-Host "EngCoder desktop started (PID $($guiProcess.Id)). Its loopback runtime is owned by EngCoder.exe." -ForegroundColor Green
            Write-Host "GUI logs: $guiStdoutLog ; $guiStderrLog" -ForegroundColor DarkGray
            Write-Host "Use --native-ui only when you explicitly want the diagnostic ImGui fallback."
        }
        "Cli" {
            Write-Host "Starting interactive EngCoder CLI with embedded runtime..." -ForegroundColor Cyan
            Push-Location $Root
            try { & $cli }
            finally { Pop-Location }
        }
        "Web" {
            $service = Start-StandaloneServer -Server $server -Cli $cli -RunDirectory $runDirectory
            Write-Host "Opening EngCoder web client: $($service.UiUrl)" -ForegroundColor Cyan
            Start-Process $service.UiUrl | Out-Null
        }
        "Server" {
            $service = Start-StandaloneServer -Server $server -Cli $cli -RunDirectory $runDirectory
            Write-Host "Standalone runtime: $($service.Url)" -ForegroundColor Green
            Write-Host "Web UI: $($service.UiUrl)"
        }
        "None" {
            Write-Host "Run skipped: build artifacts only; no server or client launched." -ForegroundColor Yellow
        }
    }
}

$cmake = Require-Command "cmake"
$ctest = Require-Command "ctest"
Require-Command "git" | Out-Null

$cmakeVersion = Get-CMakeVersion $cmake
if ($cmakeVersion -lt [version]'3.28.0') { throw "CMake 3.28+ is required. Found $cmakeVersion." }

$vs = Select-VisualStudioPreset $cmakeVersion
$env:VCPKG_ROOT = Resolve-VcpkgRoot

$configLower = $Configuration.ToLowerInvariant()
$buildPreset = "$($vs.Preset)-$configLower"
$buildDir = Join-Path $Root "out\build\$($vs.Preset)"

Write-Host "CMake:         $cmake ($cmakeVersion)"
Write-Host "Visual Studio $($vs.Version): $($vs.Path)"
Write-Host "Native project: $NativeRoot"
Write-Host "Web project:    $WebRoot"
Write-Host "Generator:      $($vs.Preset)"
Write-Host "vcpkg:          $env:VCPKG_ROOT"
Write-Host "vcpkg tag:      $VcpkgTag"
Write-Host "Configuration:  $Configuration"
Write-Host "Client:         $Client"

# Prevent MSBuild worker-node reuse and shared compiler servers from lingering
# after a completed or failed EngCoder build. This is scoped to this PowerShell
# process and therefore does not modify the user's machine-wide MSBuild setup.
$env:MSBUILDDISABLENODEREUSE = '1'
$env:UseSharedCompilation = 'false'
Stop-StaleProjectBuildProcesses -BuildDirectory $buildDir

Invoke-NativeChecked -Description "CMake configure" -FilePath $cmake -ArgumentList @("--preset", $vs.Preset) -WorkingDirectory $NativeRoot
$cleanArguments = @(
    "--build", "--preset", $buildPreset,
    "--target", "clean",
    "--", "/nr:false", "/p:UseSharedCompilation=false"
)
Invoke-NativeChecked -Description "Clean generated targets" -FilePath $cmake -ArgumentList $cleanArguments -WorkingDirectory $NativeRoot
$buildTargets = @("engcoder_server", "engcoder_gui", "engcoder_cli")
if (-not $NoTests) { $buildTargets += "engcoder_tests" }
Write-Host "Build targets:   $($buildTargets -join ', ')"

$artifactDirectory = Join-Path $buildDir $Configuration
$targetArtifacts = @{
    "engcoder_server" = (Join-Path $artifactDirectory "EngCoderServer.exe")
    "engcoder_gui" = (Join-Path $artifactDirectory "EngCoder.exe")
    "engcoder_cli" = (Join-Path $artifactDirectory "EngCoderCLI.exe")
    "engcoder_tests" = (Join-Path $artifactDirectory "engcoder_tests.exe")
}

try {
    foreach ($target in $buildTargets) {
        $targetArtifact = $targetArtifacts[$target]
        # Never let a stale executable make a failed target look healthy.
        Remove-Item -LiteralPath $targetArtifact -Force -ErrorAction SilentlyContinue
        $targetStartedUtc = [DateTime]::UtcNow

        $buildArguments = @(
            "--build", "--preset", $buildPreset,
            "--target", $target,
            "--parallel",
            "--", "/nr:false", "/p:UseSharedCompilation=false"
        )
        Invoke-NativeChecked -Description "Build $target" -FilePath $cmake -ArgumentList $buildArguments -WorkingDirectory $NativeRoot

        if (-not (Test-Path -LiteralPath $targetArtifact)) {
            throw "Build $target did not produce the expected artifact: $targetArtifact. Review the compiler/MSBuild output above for the primary failure."
        }
        $targetInfo = Get-Item -LiteralPath $targetArtifact
        if ($targetInfo.LastWriteTimeUtc -lt $targetStartedUtc.AddSeconds(-2)) {
            throw "Build $target did not refresh the expected artifact: $targetArtifact. Review the compiler/MSBuild output above for the primary failure."
        }
        Write-Host ("Built {0}: {1:N0} bytes  {2}" -f $target, $targetInfo.Length, $targetInfo.LastWriteTime) -ForegroundColor DarkGreen
        Stop-StaleProjectBuildProcesses -BuildDirectory $buildDir
    }

    if (-not $NoTests) {
        $lastTestsFailed = Join-Path $buildDir "Testing\Temporary\LastTestsFailed.log"
        Remove-Item -LiteralPath $lastTestsFailed -Force -ErrorAction SilentlyContinue
        Invoke-NativeChecked -Description "CTest" -FilePath $ctest -ArgumentList @("--preset", $buildPreset, "--output-on-failure") -WorkingDirectory $NativeRoot
        if (Test-Path -LiteralPath $lastTestsFailed) {
            $failedTests = Get-Content -LiteralPath $lastTestsFailed -Raw
            if (-not [string]::IsNullOrWhiteSpace($failedTests)) {
                throw "CTest reported failing tests. EngCoder will not be launched.`n$failedTests"
            }
        }
    }
}
finally {
    Stop-StaleProjectBuildProcesses -BuildDirectory $buildDir
}

Write-Host "Build completed successfully." -ForegroundColor Green
Write-Host "Solution: $buildDir\EngCoder.sln"

$requiredArtifacts = @(
    (Join-Path $artifactDirectory 'EngCoderServer.exe'),
    (Join-Path $artifactDirectory 'EngCoder.exe'),
    (Join-Path $artifactDirectory 'EngCoderCLI.exe')
)
if (-not $NoTests) { $requiredArtifacts += (Join-Path $artifactDirectory 'engcoder_tests.exe') }
Write-Host 'Verified build artifacts:' -ForegroundColor Cyan
foreach ($artifact in $requiredArtifacts) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected build artifact is missing: $artifact"
    }
    $info = Get-Item -LiteralPath $artifact
    Write-Host ("  {0}  {1:N0} bytes  {2}" -f $info.FullName, $info.Length, $info.LastWriteTime)
}

Remove-TransientDependencies

if (-not $NoRun) {
    Start-EngCoderSelectedClient -BuildDirectory $buildDir -ConfigurationName $Configuration
} else {
    Write-Host "Run skipped (-NoRun)."
}
