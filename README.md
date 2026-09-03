# Workshop Zephyr RTOS - ESP32-C5-DevKitC (2026)

Introdução prática ao Zephyr RTOS, de 4 horas. Placa: `esp32c5_devkitc`.

## Como acompanhar (participantes)

1. Prepare o ambiente antes do workshop, seguindo o [prework.md](prework.md).
   O download do Zephyr tem vários GB e não cabe na rede do local com todo
   mundo baixando ao mesmo tempo.
2. No dia, clone este repositório:
   ```bash
   git clone https://github.com/sylvioalves/zephyr-workshop-2026
   cd zephyr-workshop-2026
   ```
3. Abra os slides em `slides/index.html` (funciona offline) ou pelo link do
   GitHub Pages: `https://sylvioalves.github.io/zephyr-workshop-2026/`.
4. Siga os laboratórios em [labs.md](labs.md). Os Labs 2 a 6, 8 e 9 já vêm
   prontos (overlays, `prj.conf`, `main.c`) em `labs/labN-nome/`. Os
   Labs 1 e 7 usam samples da própria árvore do Zephyr.

## Arquivos deste pacote

- `README.md` (este arquivo): por onde começar.
- `prework.md`: o que instalar e baixar antes do dia do workshop.
- `readiness-check.sh` / `readiness-check.ps1`: script que confere se o seu
  ambiente está pronto. Rode depois do pré-work.
- `labs.md`: guia dos nove laboratórios, com comandos, overlays de devicetree
  e código.
- `labs/`: arquivos prontos de cada laboratório.
- `modules/ota_http/`: módulo Zephyr que baixa firmware por HTTP e grava no
  slot livre do MCUboot. Usado no Lab 9.
- `scripts/publicar-ota.py`: publica uma imagem assinada e serve por HTTP para
  as placas da sala.
- `build-flow.md`: referência profunda - o caminho completo do `west build`,
  fase por fase, com o que é lido, gerado e validado em cada uma.
- `slides/index.html`: deck autocontido (sem CDN, abre offline).
- `scripts/room-temp-display.py`: painel que escuta o anúncio BLE do Lab 8 de
  todas as placas por perto e mostra nome, temperatura e RSSI numa página que
  se atualiza sozinha. Rode com `./scripts/room-temp-display.py` e abra
  `http://127.0.0.1:8080`. Precisa de Linux com BlueZ.
- `slides/assets/`: logos e a foto do devkit. A foto
  (`esp32c5-devkitc.png`) é a isométrica oficial do ESP32-C5-DevKitC-1
  publicada pela Espressif em docs.espressif.com, recortada e reduzida para
  1000 px. Os arquivos `*-on-light.*` são as variantes de tinta escura dos
  logos, para o fundo claro do deck.
