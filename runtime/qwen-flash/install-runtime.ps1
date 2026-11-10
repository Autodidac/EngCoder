param(
    [Parameter(Mandatory = $true)][string]$StateRoot,
    [string]$ProgressFile = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Commit = '167742d9dfa01306786e7d3cfd31fcb8a8b185dc'
$Repo = 'https://github.com/GenerelSchwerz/llama.cpp.git'
$VcpkgCommit = '9e593bb18ea69cc5095e012465dcd675a822ed0d'
$VcpkgRepo = 'https://github.com/microsoft/vcpkg.git'
$RuntimeRecipe = 'engcoder-qwenflash-2026.10.06-q2pad1'
$ShortCommit = $Commit.Substring(0, 12)
$StateRoot = [System.IO.Path]::GetFullPath($StateRoot)
$RuntimeRoot = Join-Path $StateRoot 'runtimes\qwen-flash'
$RuntimeBin = Join-Path $RuntimeRoot 'bin'

if ([string]::IsNullOrWhiteSpace($ProgressFile)) {
    $ProgressFile = Join-Path $StateRoot 'runtime-install-qwen-flash.json'
}
$ProgressFile = [System.IO.Path]::GetFullPath($ProgressFile)
$ProgressDirectory = Split-Path -Parent $ProgressFile
if ($ProgressDirectory) { New-Item -ItemType Directory -Force $ProgressDirectory | Out-Null }

function Publish-RuntimeProgress(
    [int]$Percent,
    [string]$Phase,
    [string]$Message,
    [string]$Detail = '',
    [string]$State = 'installing'
) {
    $Payload = [ordered]@{
        state = $State
        percent = [Math]::Max(0, [Math]::Min(100, $Percent))
        phase = $Phase
        message = $Message
        detail = $Detail
        updated_at = (Get-Date).ToUniversalTime().ToString('o')
    }
    $TempFile = "$ProgressFile.tmp"
    $Payload | ConvertTo-Json -Compress | Set-Content -LiteralPath $TempFile -Encoding UTF8
    Move-Item -LiteralPath $TempFile -Destination $ProgressFile -Force
    Write-Host ("[EngCoder runtime] {0}% {1}: {2}" -f $Payload.percent, $Phase, $Message)
}

Publish-RuntimeProgress 2 'preflight' 'Checking build tools and CUDA' 'This updates the native llama.cpp/CUDA runtime only; Qwen model weights are unchanged.'

function Require-Tool([string]$Name) {
    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $Command) { throw "$Name is required and was not found in PATH." }
    return $Command.Source
}

function Find-CudaCompiler {
    $Command = Get-Command nvcc.exe -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }

    $Candidates = New-Object System.Collections.Generic.List[string]
    foreach ($Name in @('CUDA_PATH', 'CUDA_HOME', 'CUDA_TOOLKIT_ROOT_DIR')) {
        $Value = [Environment]::GetEnvironmentVariable($Name)
        if (-not [string]::IsNullOrWhiteSpace($Value)) { $Candidates.Add($Value) }
    }
    $CudaBase = Join-Path $env:ProgramFiles 'NVIDIA GPU Computing Toolkit\CUDA'
    if (Test-Path $CudaBase) {
        Get-ChildItem -LiteralPath $CudaBase -Directory -ErrorAction SilentlyContinue |
            Sort-Object -Property @{ Expression = {
                try { [version]($_.Name -replace '^v', '') } catch { [version]'0.0' }
            }} -Descending |
            ForEach-Object { $Candidates.Add($_.FullName) }
    }
    foreach ($Root in ($Candidates | Select-Object -Unique)) {
        $Nvcc = Join-Path $Root 'bin\nvcc.exe'
        if (Test-Path $Nvcc) {
            $env:CUDA_PATH = $Root
            $env:CUDA_HOME = $Root
            $env:Path = "$(Join-Path $Root 'bin');$env:Path"
            return $Nvcc
        }
    }
    return $null
}

function Install-CudaIfAllowed {
    if ($env:ENGCODER_AUTO_INSTALL_CUDA -ne '1') {
        throw 'CUDA Toolkit is missing. Install Nvidia.CUDA or set ENGCODER_AUTO_INSTALL_CUDA=1 and retry.'
    }
    $Winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $Winget) { throw 'CUDA Toolkit is missing and WinGet is unavailable.' }
    Write-Host 'Installing NVIDIA CUDA Toolkit (explicit EngCoder runtime-install request)...'
    & $Winget.Source install --id Nvidia.CUDA --exact --source winget --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { throw "WinGet CUDA installation failed with exit code $LASTEXITCODE." }
}


function Initialize-PinnedVcpkg([string]$CacheBase, [string]$BackendRoot) {
    $VcpkgRoot = Join-Path $CacheBase 'vcpkg'
    if ((Test-Path $VcpkgRoot) -and -not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
        Remove-Item -LiteralPath $VcpkgRoot -Recurse -Force
    }
    if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
        & $Git clone --filter=blob:none $VcpkgRepo $VcpkgRoot | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed.' }
    }
    & $Git -C $VcpkgRoot fetch origin $VcpkgCommit --depth 1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg pinned baseline fetch failed.' }
    & $Git -C $VcpkgRoot checkout --force --detach $VcpkgCommit | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg pinned baseline checkout failed.' }

    $VcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path $VcpkgExe)) {
        & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
    }

    $InstallRoot = Join-Path $BackendRoot 'vcpkg_installed'
    Push-Location $PSScriptRoot
    try {
        # This installer intentionally owns a private pinned vcpkg. Pass its root
        # explicitly so a developer-level VCPKG_ROOT cannot redirect or warn.
        & $VcpkgExe install --triplet x64-windows "--x-install-root=$InstallRoot" "--vcpkg-root=$VcpkgRoot" | Out-Host
        $InstallExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($InstallExit -ne 0) { throw "vcpkg CUDA verification failed with exit code $InstallExit." }

    $Toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path $Toolchain)) { throw "Pinned vcpkg toolchain file is missing: $Toolchain" }

    # PowerShell functions return every uncaptured pipeline value. All native
    # command output above is deliberately sent to the host so this function
    # returns exactly one object, rather than an array plus this metadata object.
    [pscustomobject]@{
        Root = $VcpkgRoot
        Toolchain = $Toolchain
        InstallRoot = $InstallRoot
    }
}

function Find-MSBuild {
    $Command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }

    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $VsWhere) {
        $Candidates = @(& $VsWhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe')
        $Candidate = $Candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1
        if ($Candidate -and (Test-Path $Candidate)) { return $Candidate }
    }

    foreach ($Edition in @('Community', 'Professional', 'Enterprise', 'BuildTools')) {
        $Candidate = Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\$Edition\MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path $Candidate) { return $Candidate }
    }
    throw 'MSBuild.exe could not be located. Install the Visual Studio 2022 C++ build tools.'
}

Publish-RuntimeProgress 5 'preflight' 'Locating Git, CMake, Visual Studio and CUDA'
$Git = Require-Tool 'git.exe'
$CMake = Require-Tool 'cmake.exe'
$Nvcc = Find-CudaCompiler
if (-not $Nvcc) {
    Install-CudaIfAllowed
    $Nvcc = Find-CudaCompiler
}
if (-not $Nvcc) { throw 'CUDA Toolkit installation completed but nvcc.exe is still unavailable.' }
Publish-RuntimeProgress 10 'cuda' 'CUDA toolkit ready' $Nvcc
Write-Host "CUDA compiler: $Nvcc"
& $Nvcc --version
if ($LASTEXITCODE -ne 0) { throw 'nvcc validation failed.' }

$CacheBase = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'EngCoderRuntime' } else { Join-Path $env:TEMP 'EngCoderRuntime' }
$BackendRoot = Join-Path $CacheBase 'qwen-flash'
$Source = Join-Path $BackendRoot "llama-$ShortCommit"
$Build = Join-Path $BackendRoot "build-$ShortCommit"
New-Item -ItemType Directory -Force $BackendRoot | Out-Null
Publish-RuntimeProgress 18 'dependencies' 'Preparing pinned vcpkg toolchain' 'The first run may clone vcpkg and verify CUDA dependencies.'
$VcpkgResults = @(Initialize-PinnedVcpkg $CacheBase $BackendRoot)
$Vcpkg = $VcpkgResults |
    Where-Object {
        $_ -and
        $_.PSObject.Properties['Toolchain'] -and
        $_.PSObject.Properties['InstallRoot']
    } |
    Select-Object -Last 1
if (-not $Vcpkg) {
    throw 'Pinned vcpkg initialization did not return valid toolchain metadata.'
}
Publish-RuntimeProgress 38 'dependencies' 'Pinned dependencies ready' "vcpkg baseline $VcpkgCommit"
Write-Host "vcpkg: $VcpkgCommit"

Publish-RuntimeProgress 42 'backend-source' 'Preparing pinned Qwen llama.cpp backend source' 'Git clone/fetch progress is shown in the installer output while this phase is active.'
if ((Test-Path $Source) -and -not (Test-Path (Join-Path $Source '.git'))) {
    Remove-Item -LiteralPath $Source -Recurse -Force
}
if (-not (Test-Path (Join-Path $Source '.git'))) {
    & $Git -c core.longpaths=true clone --filter=blob:none --no-checkout $Repo $Source
    if ($LASTEXITCODE -ne 0) { throw 'llama.cpp clone failed.' }
}
& $Git -C $Source config core.longpaths true
& $Git -C $Source fetch origin $Commit --depth 1
if ($LASTEXITCODE -ne 0) { throw 'llama.cpp commit fetch failed.' }
& $Git -C $Source checkout --force --detach $Commit
if ($LASTEXITCODE -ne 0) { throw 'llama.cpp pinned checkout failed.' }

$Patch = Join-Path $PSScriptRoot 'patches\0001-qwen38-q2-padded-down.patch'
if (-not (Test-Path $Patch)) { throw "Qwen3.8 Q2 compatibility patch is missing: $Patch" }
& $Git -C $Source apply --check $Patch
if ($LASTEXITCODE -ne 0) { throw 'Qwen3.8 Q2 compatibility patch no longer applies cleanly to the pinned backend.' }
& $Git -C $Source apply $Patch
if ($LASTEXITCODE -ne 0) { throw 'Qwen3.8 Q2 compatibility patch failed.' }
Publish-RuntimeProgress 55 'compatibility' 'Qwen compatibility patch applied'
Write-Host 'Applied Qwen3.8 Q2 padded-down compatibility patch.'

Publish-RuntimeProgress 60 'configure' 'Configuring CUDA backend build' 'CMake is generating the pinned runtime project.'
if (Test-Path $Build) { Remove-Item -LiteralPath $Build -Recurse -Force }
& $CMake -S $Source -B $Build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$($Vcpkg.Toolchain)" `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DVCPKG_MANIFEST_MODE=OFF `
    "-DVCPKG_INSTALLED_DIR=$($Vcpkg.InstallRoot)" `
    -DGGML_CUDA=ON `
    -DGGML_NATIVE=ON `
    -DLLAMA_BUILD_COMMON=ON `
    -DLLAMA_BUILD_SERVER=ON `
    -DLLAMA_BUILD_APP=OFF `
    -DLLAMA_BUILD_TESTS=OFF `
    -DLLAMA_BUILD_EXAMPLES=OFF `
    -DLLAMA_BUILD_TOOLS=ON `
    -DLLAMA_BUILD_UI=OFF `
    -DLLAMA_BUILD_BORINGSSL=ON
if ($LASTEXITCODE -ne 0) { throw 'llama.cpp configure failed.' }

$ServerProjects = @(Get-ChildItem -LiteralPath $Build -Filter 'llama-server.vcxproj' -File -Recurse)
if ($ServerProjects.Count -ne 1) {
    throw "Expected exactly one generated llama-server.vcxproj; found $($ServerProjects.Count)."
}
$ServerProject = $ServerProjects[0].FullName
$ReleaseConfigLine = Select-String -LiteralPath $ServerProject -Pattern 'ProjectConfiguration Include="Release\|([^"]+)"' | Select-Object -First 1
if (-not $ReleaseConfigLine -or $ReleaseConfigLine.Matches.Count -eq 0) {
    throw "Could not determine Release platform from $ServerProject"
}
$Platform = $ReleaseConfigLine.Matches[0].Groups[1].Value
$MSBuild = Find-MSBuild
Publish-RuntimeProgress 72 'build' 'Compiling Qwen Runtime Backend' "Release|$Platform · this is normally the longest CPU/GPU-toolchain phase."
Write-Host "Building: $ServerProject (Release|$Platform)"
& $MSBuild $ServerProject /m /nologo /verbosity:minimal /p:Configuration=Release "/p:Platform=$Platform"
if ($LASTEXITCODE -ne 0) { throw 'llama-server build failed.' }

$Bin = @(
    (Join-Path $Build 'bin\Release'),
    (Join-Path $Build 'bin'),
    (Join-Path $Build 'bin\RelWithDebInfo')
) | Where-Object { Test-Path (Join-Path $_ 'llama-server.exe') } | Select-Object -First 1
if (-not $Bin) { throw 'llama-server.exe was built but could not be located.' }

Publish-RuntimeProgress 94 'stage' 'Staging runtime binaries' 'The completed backend is being copied into EngCoder runtime storage.'
$Staging = "$RuntimeBin.staging"
Remove-Item -LiteralPath $Staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Staging | Out-Null
Copy-Item -Path (Join-Path $Bin '*') -Destination $Staging -Recurse -Force
if (-not (Test-Path (Join-Path $Staging 'llama-server.exe'))) { throw 'Runtime staging failed.' }
Remove-Item -LiteralPath $RuntimeBin -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Split-Path -Parent $RuntimeBin) | Out-Null
Move-Item -LiteralPath $Staging -Destination $RuntimeBin
$RuntimeManifest = @{
    runtime = 'qwen-flash'
    backend_commit = $Commit
    build_recipe = $RuntimeRecipe
    source = 'engcoder-installer'
    features = @('qwen38-q2-padded-down-v1')
} | ConvertTo-Json
$RuntimeManifest | Set-Content -LiteralPath (Join-Path $RuntimeRoot 'runtime.json') -Encoding UTF8
Publish-RuntimeProgress 100 'complete' 'Qwen Runtime Backend ready' $RuntimeBin 'installed'
Write-Host "Qwen Flash runtime installed: $RuntimeBin"
Write-Host "Runtime recipe: $RuntimeRecipe"
