# Readiness check for the Zephyr ESP32-C5 workshop (Windows PowerShell).
# Run from an activated venv, inside ~\zephyrproject\zephyr:
#   powershell -ExecutionPolicy Bypass -File readiness-check.ps1
# It verifies the tools and builds a sample for the board offline (no board
# needed). Participant-facing messages are in Portuguese; the success line is
# "PRONTO:". This file is UTF-8 with BOM so PowerShell 5.1 parses accents.

# Make accented output render correctly in the Windows console.
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$Board = "esp32c5_devkitc/esp32c5/hpcore"
$fail = $false

function Test-Cmd($name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }
function Ok($m)  { Write-Host "  ok    $m" }
function Bad($m) { Write-Host "  FALHA $m"; $script:fail = $true }

Write-Host "== Teste de prontidão do workshop Zephyr C5 =="

if (Test-Cmd python) { Ok ("python: " + (python --version 2>&1)) } else { Bad "python não encontrado" }
if (Test-Cmd west)   { Ok ("west: "   + (west --version 2>&1)) }   else { Bad "west não encontrado (o venv está ativado?)" }
if (Test-Cmd cmake)  { Ok ("cmake: "  + ((cmake --version 2>&1) | Select-Object -First 1)) } else { Bad "cmake não encontrado" }
if (Test-Cmd ninja)  { Ok ("ninja: "  + (ninja --version 2>&1)) }  else { Bad "ninja não encontrado" }

$sdk = (west sdk list 2>$null | Out-String)
if ($sdk -match "riscv64") { Ok "toolchain RISC-V do Zephyr SDK presente" }
else { Bad "toolchain RISC-V não encontrado (rode: west sdk install -t riscv64-zephyr-elf)" }

if ($fail) {
  Write-Host ""
  Write-Host "Uma ou mais ferramentas faltam. Corrija as linhas FALHA acima e rode de novo."
  Write-Host "NÃO-PRONTO"
  exit 1
}

Write-Host ""
Write-Host "Compilando samples/hello_world para $Board (offline, sem placa)..."
$log = Join-Path $env:TEMP "c5_build.log"
west build -p -b $Board samples/hello_world *> $log

if (Test-Path "build\zephyr\zephyr.elf") {
  Ok "build gerou build\zephyr\zephyr.elf"
  Write-Host ""
  Write-Host "PRONTO: ambiente de build do workshop está ok"
  exit 0
}

Write-Host ""
Write-Host "A build FALHOU. Últimas 25 linhas do log de build:"
Get-Content $log -Tail 25
Write-Host ""
Write-Host "NÃO-PRONTO"
exit 1
