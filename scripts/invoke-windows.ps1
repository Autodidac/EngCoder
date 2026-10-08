$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$parseFailed = $false
Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File | ForEach-Object {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $_.FullName,
        [ref]$tokens,
        [ref]$errors
    )

    if ($errors -and $errors.Count -gt 0) {
        $parseFailed = $true
        foreach ($parseError in $errors) {
            $line = $parseError.Extent.StartLineNumber
            $column = $parseError.Extent.StartColumnNumber
            Write-Host ("PowerShell parse error: {0}:{1}:{2}: {3}" -f $_.FullName, $line, $column, $parseError.Message) -ForegroundColor Red
        }
    }
}

if ($parseFailed) {
    exit 2
}

$buildScript = Join-Path $PSScriptRoot 'build-windows.ps1'
& $buildScript @args
