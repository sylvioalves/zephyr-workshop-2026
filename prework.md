# Pré-work: Workshop Zephyr (ESP32-C5)

Tempo de leitura: 10 minutos. Tempo de setup: 30 a 60 minutos, mais o tempo de
download.

**Por que isso importa:** o download do Zephyr tem vários GB. Se 20 pessoas
baixarem tudo na rede do local ao mesmo tempo, ninguém consegue trabalhar.
Por favor, faça os passos abaixo em casa/no trabalho, numa conexão boa, rode o
teste de prontidão e me envie a última linha que ele imprime. Se travar, me
chame ANTES do workshop: não teremos tempo de arrumar notebooks durante a sessão.

## O que você precisa

- Um notebook (Linux, Windows ou macOS, todos servem) com cerca de 10 GB livres.
- Sua placa ESP32-C5-DevKitC e um cabo USB de dados (cabo só de carga não serve).
- Direitos de administrador para instalar pacotes e, no Windows, mudar duas
  configurações.

Para este workshop usamos o **Zephyr `main`** e o **Zephyr SDK 1.0.x**. Os
comandos do Passo 2 já trazem isso; use-os como estão para todo mundo ficar
igual. (Num produto você fixaria uma tag, por exemplo `v4.4.2`, em vez de
acompanhar a `main` - o Lab 9 volta nesse assunto.)

## Passo 1 - Instale os pacotes do sistema operacional

### Linux (Ubuntu 24.04 ou mais novo)

```bash
sudo apt update
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv \
  python3-tk xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev \
  libmagic1
```

Em host Linux ARM64, remova `gcc-multilib g++-multilib`.

### macOS (Homebrew)

```bash
brew install cmake ninja gperf python3 python-tk ccache qemu dtc libmagic \
  wget openocd
```

Apple Silicon usa o prefixo `/opt/homebrew`; Intel usa `/usr/local`. Garanta
que o Python do Homebrew está no PATH.

### Windows

Rode num terminal de Administrador:

```powershell
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf `
  Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip
```

Depois faça estas duas coisas específicas do Windows (sem elas a build falha):

1. Habilite caminhos longos:
   ```powershell
   git config --system core.longpaths true
   ```
   Defina também o registro `LongPathsEnabled` como 1 (procure "Enable long
   paths" na Política de Grupo, ou ajuste
   `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled` = 1).
2. Permita ativar o venv no PowerShell:
   ```powershell
   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
   ```
   (Ou use o `cmd.exe` e `activate.bat` em vez do PowerShell.)

Use **Python 3.12**. Versões muito novas do Python às vezes quebram pacotes.

## Passo 2 - Baixe o Zephyr e as ferramentas

Igual em todas as plataformas depois dos pacotes instalados. No Windows use
PowerShell; troque `source .../activate` por `.venv\Scripts\Activate.ps1`.

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west

west init -m https://github.com/zephyrproject-rtos/zephyr ~/zephyrproject
cd ~/zephyrproject

# Clone raso - economiza vários GB de histórico que você não precisa:
west update -o=--depth=1

west packages pip --install
west zephyr-export

cd ~/zephyrproject/zephyr

# Somente toolchain RISC-V - o C5 não precisa de outra arquitetura:
west sdk install -t riscv64-zephyr-elf

# Blobs binários de RF - necessários para os labs de Wi-Fi e BLE:
west blobs fetch hal_espressif
```

Lembre: você precisa reativar o venv
(`source ~/zephyrproject/.venv/bin/activate`) em cada novo terminal.

## Passo 3 - Driver serial e permissões

- **Linux:** adicione seu usuário ao grupo `dialout` e instale as regras udev
  que vêm com o SDK; depois saia e entre de novo na sessão:
  ```bash
  sudo usermod -aG dialout $USER
  ```
  (A instalação do SDK imprime o comando das regras udev; rode-o.)
- **Windows / macOS:** se a placa usa uma ponte USB-UART CP210x ou CH34x,
  instale esse driver do fabricante. Se a placa aparece como dispositivo USB
  nativo (uma porta `usbmodem`/`COM` que simplesmente surge), não precisa de
  driver. Na dúvida, instale o driver CP210x: é o caso mais comum.

## Passo 4 - Rode o teste de prontidão

Baixe `readiness-check.sh` (Linux/macOS) ou `readiness-check.ps1` (Windows) que
enviei, e a partir do venv ativado:

```bash
# Linux / macOS
cd ~/zephyrproject/zephyr
bash /caminho/para/readiness-check.sh
```

```powershell
# Windows
cd ~\zephyrproject\zephyr
powershell -ExecutionPolicy Bypass -File C:\caminho\para\readiness-check.ps1
```

Ele compila um programa pequeno para a placa offline (não precisa da placa) e,
se der certo, imprime:

```
PRONTO: ambiente de build do workshop está ok
```

**Me envie essa linha exata** (ou o texto do erro, se falhar) pelo menos um dia
antes do workshop. Assim eu corrijo problemas com antecedência em vez de durante
a sessão.

## Opcional, mas bom: confirme que a gravação funciona

Se tiver a placa em mãos, conecte e tente (a partir do venv ativado, em
`~/zephyrproject/zephyr`):

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore samples/hello_world
west flash
west espressif monitor
```

O `west espressif monitor` abre o console já na velocidade certa e detecta a
porta sozinho; para sair, use `Ctrl+]`. Se ele escolher a porta errada, force
com `west espressif monitor -p /dev/ttyUSB1` (ou o COM equivalente no Windows).

Atenção: o console fica na porta UART, não necessariamente na porta USB-JTAG.
Se não aparecer nada, tente o outro conector USB. Vamos cobrir isso no dia;
não se preocupe se a gravação estiver chata agora.

## Se o seu download estiver enorme ou lento

O clone raso do Passo 2 (`--depth=1`) é o que mais economiza banda. Se ainda
assim não conseguir concluir o setup em casa, me avise: terei um pen drive com
tudo em cache no local, como reserva. Mas tente em casa primeiro; passar o pen
drive por 20 notebooks é lento.
