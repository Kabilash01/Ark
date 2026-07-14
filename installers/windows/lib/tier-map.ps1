$gpu = (Get-WmiObject Win32_VideoController |
        Where-Object {$_.AdapterRAM -gt 0} |
        Select-Object -First 1).AdapterRAM
$vram_gb = [math]::Round($gpu / 1GB, 1)

if     ($vram_gb -ge 22) { $tier = "T4" }
elseif ($vram_gb -ge 14) { $tier = "T3" }
elseif ($vram_gb -ge 6)  { $tier = "T2" }
elseif ($vram_gb -ge 3)  { $tier = "T1" }
else                      { $tier = "T5" }

Write-Output "Detected ${vram_gb}GB VRAM -> Tier $tier"
$tier | Out-File -FilePath "config\active-tier.txt"
