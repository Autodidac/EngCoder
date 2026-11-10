param(
    [string]$BaseUrl = "http://127.0.0.1:14321",
    [string]$Model = "engcoder-foundation",
    [string]$PromptFile = "",
    [int]$TimeoutSeconds = 10800,
    [string]$OutputDirectory = "out/qualification"
)

$ErrorActionPreference = "Stop"

$prompt = @'
You are serving EpochEngine through EngCoder's inference-only endpoint.
Produce a source-change proposal only. Do not write files, execute commands, or claim changes were applied.
Return a concise implementation proposal using the supplied tool contract when a structured proposal is appropriate.
Task: inspect the provided request context and propose a safe C++23 source change that preserves Epoch's sandbox and existing architecture.
'@
if ($PromptFile) {
    $prompt = Get-Content -LiteralPath $PromptFile -Raw
}

$tool = @{
    type = "function"
    function = @{
        name = "propose_source_patch"
        description = "Return a source patch proposal without applying it."
        parameters = @{
            type = "object"
            additionalProperties = $false
            properties = @{
                summary = @{ type = "string" }
                files = @{
                    type = "array"
                    items = @{
                        type = "object"
                        additionalProperties = $false
                        properties = @{
                            path = @{ type = "string" }
                            rationale = @{ type = "string" }
                            proposed_change = @{ type = "string" }
                        }
                        required = @("path", "rationale", "proposed_change")
                    }
                }
            }
            required = @("summary", "files")
        }
    }
}

$body = @{
    model = $Model
    stream = $false
    messages = @(@{ role = "user"; content = $prompt })
    tools = @($tool)
    tool_choice = @{ type = "function"; function = @{ name = "propose_source_patch" } }
    parallel_tool_calls = $false
    max_completion_tokens = 32768
    temperature = 0.10
    timeout_seconds = $TimeoutSeconds
} | ConvertTo-Json -Depth 20 -Compress

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outPath = Join-Path $OutputDirectory "epoch-source-proposal-$stamp.json"
$started = [DateTimeOffset]::UtcNow
$timer = [System.Diagnostics.Stopwatch]::StartNew()

try {
    $response = Invoke-WebRequest -Uri "$BaseUrl/v1/chat/completions" -Method Post -ContentType "application/json" -Body $body -TimeoutSec ([Math]::Max(1, $TimeoutSeconds + 30))
    $timer.Stop()
    $record = [ordered]@{
        qualified = $true
        endpoint = "$BaseUrl/v1/chat/completions"
        model = $Model
        request_id = $response.Headers["X-Request-ID"]
        http_status = [int]$response.StatusCode
        elapsed_ms = $timer.ElapsedMilliseconds
        started_utc = $started.ToString("o")
        completed_utc = [DateTimeOffset]::UtcNow.ToString("o")
        response = ($response.Content | ConvertFrom-Json)
    }
    $record | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $outPath -Encoding UTF8
    Write-Host "Epoch inference qualification passed: $outPath"
    exit 0
}
catch {
    $timer.Stop()
    $status = $null
    $requestId = $null
    $responseBody = $null
    if ($_.Exception.Response) {
        try { $status = [int]$_.Exception.Response.StatusCode } catch {}
        try { $requestId = $_.Exception.Response.Headers["X-Request-ID"] } catch {}
    }
    if ($_.ErrorDetails.Message) {
        try { $responseBody = $_.ErrorDetails.Message | ConvertFrom-Json } catch { $responseBody = $_.ErrorDetails.Message }
    }
    $record = [ordered]@{
        qualified = $false
        endpoint = "$BaseUrl/v1/chat/completions"
        model = $Model
        request_id = $requestId
        http_status = $status
        elapsed_ms = $timer.ElapsedMilliseconds
        started_utc = $started.ToString("o")
        failed_utc = [DateTimeOffset]::UtcNow.ToString("o")
        error = $_.Exception.Message
        response = $responseBody
    }
    $record | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $outPath -Encoding UTF8
    Write-Error "Epoch inference qualification failed. Exact result: $outPath"
    exit 1
}
