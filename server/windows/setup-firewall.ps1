# Creates a narrow inbound rule for the game server: UDP <port> from the local subnet only.
# Run once as administrator (it re-launches itself elevated):
#   powershell -ExecutionPolicy Bypass -File server\windows\setup-firewall.ps1 [-Port 4499] [-GodEye]
# -GodEye also allows GodEye's web page (TCP 8080) from the local subnet, for phones and other PCs.
# Rollback:  Remove-NetFirewallRule -DisplayName 'GTA Local Online GodEye (TCP)'
# Rollback:  Remove-NetFirewallRule -DisplayName 'GTA Local Online (UDP)'
param([int]$Port = 4499, [switch]$GodEye, [int]$GodEyePort = 8080)
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`" -Port $Port$(if ($GodEye) { ' -GodEye' })"
    exit
}
$name = 'GTA Local Online (UDP)'
Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow -Protocol UDP -LocalPort $Port `
    -RemoteAddress LocalSubnet -Profile Private, Public | Out-Null
Write-Host "Rule '$name' created: inbound UDP $Port from LocalSubnet."
if ($GodEye) {
    $ge = 'GTA Local Online GodEye (TCP)'
    Get-NetFirewallRule -DisplayName $ge -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    New-NetFirewallRule -DisplayName $ge -Direction Inbound -Action Allow -Protocol TCP -LocalPort $GodEyePort `
        -RemoteAddress LocalSubnet -Profile Private, Public | Out-Null
    Write-Host "Rule '$ge' created: inbound TCP $GodEyePort from LocalSubnet."
}
Write-Host "Windows also auto-created broad 'localonlineserver.exe' rules when the server first ran;"
Write-Host "they allow every port from anywhere. Remove them for the narrow rule to be the only one:"
Write-Host "  Get-NetFirewallApplicationFilter | ? Program -like '*localonlineserver*' | Get-NetFirewallRule | Remove-NetFirewallRule"
Read-Host 'Press Enter to close'
