#!/usr/bin/env bash
# Readiness check for the Zephyr ESP32-C5 workshop (Linux / macOS).
# Run from an activated venv, inside ~/zephyrproject/zephyr:
#   bash readiness-check.sh
# It verifies the tools and builds a sample for the board offline (no board
# needed). Participant-facing messages are in Portuguese; the success line is
# "PRONTO:". This file is UTF-8.

set -u
BOARD="esp32c5_devkitc/esp32c5/hpcore"
fail=0

say()  { printf '%s\n' "$*"; }
ok()   { printf '  ok    %s\n' "$*"; }
bad()  { printf '  FALHA %s\n' "$*"; fail=1; }

say "== Teste de prontidão do workshop Zephyr C5 =="

if command -v python3 >/dev/null 2>&1; then
  ok "python3: $(python3 --version 2>&1)"
else
  bad "python3 não encontrado"
fi

if command -v west >/dev/null 2>&1; then
  ok "west: $(west --version 2>&1)"
else
  bad "west não encontrado (o venv está ativado?)"
fi

if command -v cmake >/dev/null 2>&1; then
  ok "cmake: $(cmake --version 2>&1 | head -n1)"
else
  bad "cmake não encontrado"
fi

if command -v ninja >/dev/null 2>&1; then
  ok "ninja: $(ninja --version 2>&1)"
else
  bad "ninja não encontrado"
fi

if west sdk list 2>/dev/null | grep -qi "riscv64"; then
  ok "toolchain RISC-V do Zephyr SDK presente"
else
  bad "toolchain RISC-V não encontrado (rode: west sdk install -t riscv64-zephyr-elf)"
fi

if [ "$fail" -ne 0 ]; then
  say ""
  say "Uma ou mais ferramentas faltam. Corrija as linhas FALHA acima e rode de novo."
  say "NÃO-PRONTO"
  exit 1
fi

say ""
say "Compilando samples/hello_world para ${BOARD} (offline, sem placa)..."
if west build -p -b "${BOARD}" samples/hello_world >/tmp/c5_build.log 2>&1; then
  if [ -f build/zephyr/zephyr.elf ]; then
    ok "build gerou build/zephyr/zephyr.elf"
    say ""
    say "PRONTO: ambiente de build do workshop está ok"
    exit 0
  fi
fi

say ""
say "A build FALHOU. Últimas 25 linhas do log de build:"
tail -n 25 /tmp/c5_build.log
say ""
say "NÃO-PRONTO"
exit 1
