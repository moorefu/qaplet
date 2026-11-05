param(
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [Parameter(Mandatory=$true)][long]$StartUnixMilliseconds,
    [int]$IntervalMilliseconds = 1000
)
$ErrorActionPreference = 'Stop'
$writer = [System.IO.StreamWriter]::new($OutputPath, $false, [System.Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
try {
    $target = [System.Diagnostics.Process]::GetProcessById($ProcessId)
    while (-not $target.HasExited) {
        $target.Refresh()
        $threads = @()
        foreach ($thread in $target.Threads) {
            try {
                $state = $thread.ThreadState.ToString()
                $reason = $null
                if ($state -eq 'Wait') { $reason = $thread.WaitReason.ToString() }
                $threads += @{tid=$thread.Id; state=$state; wait_reason=$reason}
            } catch {
                # A real thread may retire between enumeration and inspection.
                $threads += @{tid=$thread.Id; error=$_.Exception.Message}
            }
        }
        $row = @{
            elapsed_seconds=([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-$StartUnixMilliseconds)/1000.0
            process_id=$ProcessId
            resources=@{
                source='System.Diagnostics.Process: TotalProcessorTime/WorkingSet64/HandleCount/Threads.ThreadState.WaitReason'
                cpu_seconds=$target.TotalProcessorTime.TotalSeconds
                rss_bytes=$target.WorkingSet64
                handles=$target.HandleCount
                threads=$target.Threads.Count
                thread_waits=$threads
            }
        }
        $writer.WriteLine(($row | ConvertTo-Json -Compress -Depth 6))
        Start-Sleep -Milliseconds $IntervalMilliseconds
    }
} catch {
    if ($null -eq $target -or -not $target.HasExited) {
        $writer.WriteLine((@{sampler_error=$_.Exception.Message} | ConvertTo-Json -Compress))
        exit 1
    }
} finally {
    if ($null -ne $target) { $target.Dispose() }
    $writer.Dispose()
}
