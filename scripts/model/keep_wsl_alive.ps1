[CmdletBinding()]
param([int]$ClientProcessId = 0)

function Start-DarkFactoryWslKeepAlive {
    # systemd services (even with lingering) do not keep a WSL VM running.
    # A foreground Linux process owned by this Windows launcher does.
    return Start-Process -FilePath 'wsl.exe' -ArgumentList @('-d','Ubuntu','--exec','sleep','infinity') -WindowStyle Hidden -PassThru
}

function Stop-DarkFactoryWslKeepAlive {
    param([System.Diagnostics.Process]$KeepAlive)
    if ($KeepAlive -and -not $KeepAlive.HasExited) {
        Stop-Process -Id $KeepAlive.Id -ErrorAction SilentlyContinue
    }
}

# Attach to an already-running demo without restarting its agent conversation.
if ($ClientProcessId -gt 0) {
    $Client = Get-Process -Id $ClientProcessId -ErrorAction Stop
    if ($Client.ProcessName -ne 'UnrealEditor') { throw 'Expected an UnrealEditor demo process' }
    $KeepAlive = Start-DarkFactoryWslKeepAlive
    try { $Client.WaitForExit() }
    finally { Stop-DarkFactoryWslKeepAlive $KeepAlive }
}
