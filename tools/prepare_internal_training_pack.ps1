[CmdletBinding()]
param(
    [string]$Python = "",
    [string]$Destination = "",
    [string]$Wheelhouse = "",
    [string]$TorchIndexUrl = "",
    [switch]$AllowCpuTraining
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Destination)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA is not available; pass -Destination explicitly."
    }
    $Destination = Join-Path $env:LOCALAPPDATA "EngCoder\pack-inbox\internal-pytorch-training"
}
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$destinationParent = Split-Path -Parent $destinationPath
New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
if (Test-Path -LiteralPath $destinationPath) {
    throw "Destination already exists. Preserve or move it before preparing another pack: $destinationPath"
}

$pythonCommand = $Python
$pythonPrefix = @()
if ([string]::IsNullOrWhiteSpace($pythonCommand)) {
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $launcher) {
        $pythonCommand = $launcher.Source
        $pythonPrefix = @("-3.11")
    } else {
        $resolvedPython = Get-Command python.exe -ErrorAction Stop
        $pythonCommand = $resolvedPython.Source
    }
} else {
    $pythonCommand = (Resolve-Path -LiteralPath $pythonCommand).Path
}

$staging = Join-Path $destinationParent (".internal-pytorch-training-{0}-{1}" -f $PID, [DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
$runtime = Join-Path $staging "runtime"
$backend = Join-Path $staging "backend"

try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    & $pythonCommand @pythonPrefix -m venv --copies $runtime
    if ($LASTEXITCODE -ne 0) { throw "Python could not create the isolated training runtime." }

    $packPython = Join-Path $runtime "Scripts\python.exe"
    if (-not (Test-Path -LiteralPath $packPython -PathType Leaf)) {
        throw "The prepared runtime did not contain Scripts\python.exe. Use Windows Python 3.11."
    }
    & $packPython -m pip install --disable-pip-version-check --upgrade pip
    if ($LASTEXITCODE -ne 0) { throw "pip bootstrap failed." }

    $installArguments = @("-m", "pip", "install", "--disable-pip-version-check")
    if (-not [string]::IsNullOrWhiteSpace($Wheelhouse) -and
        -not [string]::IsNullOrWhiteSpace($TorchIndexUrl)) {
        throw "Use either -Wheelhouse or -TorchIndexUrl, not both."
    }
    if (-not [string]::IsNullOrWhiteSpace($Wheelhouse)) {
        $resolvedWheelhouse = (Resolve-Path -LiteralPath $Wheelhouse).Path
        $installArguments += @("--no-index", "--find-links", $resolvedWheelhouse)
    }
    if (-not [string]::IsNullOrWhiteSpace($TorchIndexUrl)) {
        $parsedIndex = $null
        if (-not [Uri]::TryCreate($TorchIndexUrl, [UriKind]::Absolute, [ref]$parsedIndex) -or
            $parsedIndex.Scheme -ne "https" -or
            $parsedIndex.Host -ne "download.pytorch.org" -or
            -not $parsedIndex.IsDefaultPort -or
            -not [string]::IsNullOrEmpty($parsedIndex.Query) -or
            -not [string]::IsNullOrEmpty($parsedIndex.Fragment) -or
            -not $parsedIndex.AbsolutePath.StartsWith("/whl/", [StringComparison]::Ordinal)) {
            throw "TorchIndexUrl must be an HTTPS wheel index under https://download.pytorch.org/whl/."
        }
        # The official PyTorch wheel index intentionally carries Torch builds,
        # not unrelated dependencies such as Safetensors and NumPy. Install
        # those required serialization dependencies from the normal Python
        # index first, then constrain only Torch to the trusted CUDA channel.
        & $packPython -m pip install --disable-pip-version-check numpy safetensors
        if ($LASTEXITCODE -ne 0) { throw "NumPy and Safetensors could not be installed." }
        $installArguments += @("--index-url", $parsedIndex.AbsoluteUri.TrimEnd('/'))
        $installArguments += "torch"
    } else {
        $installArguments += @("torch", "numpy", "safetensors")
    }
    & $packPython @installArguments
    if ($LASTEXITCODE -ne 0) { throw "PyTorch training dependencies could not be installed." }

    New-Item -ItemType Directory -Path $backend | Out-Null
    foreach ($name in @(
        "model_worker.py",
        "internal_model.py",
        "curriculum_guard.py",
        "curriculum_compiler.py")) {
        Copy-Item -LiteralPath (Join-Path $repository "resources\$name") -Destination (Join-Path $backend $name)
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $frozenRequirements = @(& $packPython -m pip freeze --all)
    [System.IO.File]::WriteAllLines(
        (Join-Path $backend "requirements.freeze.txt"),
        [string[]]$frozenRequirements,
        $utf8NoBom)
    $doctor = & $packPython (Join-Path $backend "model_worker.py") doctor 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "The prepared backend failed its doctor check: $($doctor -join ' ')"
    }
    & $packPython -c "import numpy as np; x=np.arange(8, dtype=np.float32); raise SystemExit(0 if float((x*x).sum()) == 140.0 else 4)"
    if ($LASTEXITCODE -ne 0) {
        throw "The prepared backend failed its NumPy operation check."
    }
    $serializationProbe = Join-Path $staging "serialization-smoke.safetensors"
    & $packPython -c "import sys, torch; from safetensors.torch import load_file, save_file; p=sys.argv[1]; save_file({'probe': torch.arange(4, dtype=torch.float32)}, p); raise SystemExit(0 if torch.equal(load_file(p)['probe'], torch.arange(4, dtype=torch.float32)) else 4)" $serializationProbe
    if ($LASTEXITCODE -ne 0) {
        throw "The prepared backend failed its Safetensors round-trip check."
    }
    Remove-Item -LiteralPath $serializationProbe -Force
    if (-not $AllowCpuTraining) {
        & $packPython -c "import sys, torch; (sys.exit(3) if not torch.cuda.is_available() else None); x=torch.ones((8,8), device='cuda'); y=x@x; torch.cuda.synchronize(); raise SystemExit(0 if float(y[0,0]) == 8.0 else 4)"
        if ($LASTEXITCODE -ne 0) {
            throw "PyTorch cannot see CUDA. Rebuild with a compatible CUDA wheel, or pass -AllowCpuTraining only for slow smoke tests."
        }
    }

    $buildReceipt = [ordered]@{
        format = "engcoder.internal-training-pack-source.v2"
        created_utc = [DateTimeOffset]::UtcNow.ToString("o")
        source_repository = $repository
        python = (& $packPython -c "import sys; print(sys.version.replace(chr(10), ' '))")
        torch = (& $packPython -c "import torch; print(torch.__version__)")
        cuda_available = ((& $packPython -c "import torch; print('true' if torch.cuda.is_available() else 'false')") -eq "true")
        torch_index = if ([string]::IsNullOrWhiteSpace($TorchIndexUrl)) { "default-or-wheelhouse" } else { $parsedIndex.AbsoluteUri.TrimEnd('/') }
        contains_model_weights = $false
        required_backend_files = @(
            "model_worker.py",
            "internal_model.py",
            "curriculum_guard.py",
            "curriculum_compiler.py")
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $staging "build-receipt.json"),
        (($buildReceipt | ConvertTo-Json -Depth 4) + "`n"),
        $utf8NoBom)

    Move-Item -LiteralPath $staging -Destination $destinationPath
    Write-Host "Prepared Internal PyTorch Training pack: $destinationPath"
    Write-Host "Start EngCoder; it will verify and transactionally install this staged payload before any queued weight job runs."
} catch {
    if (Test-Path -LiteralPath $staging) {
        $resolvedStaging = [System.IO.Path]::GetFullPath($staging)
        $resolvedParent = [System.IO.Path]::GetFullPath($destinationParent).TrimEnd('\') + '\'
        if ($resolvedStaging.StartsWith($resolvedParent, [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedStaging -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    throw
}
