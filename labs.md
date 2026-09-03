# Laboratórios - Workshop Zephyr (ESP32-C5)

Todos os labs assumem: venv ativado, diretório de trabalho
`~/zephyrproject/zephyr`, alvo de placa `esp32c5_devkitc/esp32c5/hpcore`. Os
arquivos prontos de cada lab estão em `labs/labN-nome/`.

> **Nenhum componente externo é necessário.** Só a placa e o cabo USB. Usamos o
> botão de boot, o console serial, o sensor de temperatura interno do SoC, o
> LED RGB embutido e os rádios Wi-Fi e BLE. No Lab 8, o segundo dispositivo é
> o seu celular; no Lab 9, é o computador do instrutor servindo o firmware.

Padrão de compilar/gravar usado o tempo todo:

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore <dir-do-app>
west flash
west espressif monitor
```

Abra o console serial a **115200** baud. Se não aparecer nada depois de gravar,
tente o outro conector USB: o console fica na **UART0**, que pode não ser a
mesma porta física por onde você grava.

Vamos repetir sempre: `-p` força uma build limpa (pristine). Iniciantes
economizam horas usando isso sempre que mudam de placa, overlay ou Kconfig.

---

## Fluxo de trabalho (antes do Lab 1)

Quatro ideias que valem o dia inteiro:

**1. Anatomia de uma aplicação.** São só quatro arquivos:

```
minha-app/
├── CMakeLists.txt   registra a aplicação e os fontes
├── prj.conf         Kconfig: o que compilar junto
├── src/main.c       o seu código
└── boards/
    └── esp32c5_devkitc_hpcore.overlay   devicetree: hardware adicional
```

O nome do overlay tem que bater **exatamente** com o alvo da placa. Se não
bater, ele é ignorado em silêncio: a build passa e o hardware não funciona.

**2. O pipeline.** Devicetree + Kconfig + seu código -> CMake/Ninja ->
`zephyr.elf`. Devicetree e Kconfig não são compilados: eles **geram cabeçalhos**
que o seu código usa. Por isso mudanças neles exigem build limpa.

O `west build` roda 8 fases, nesta ordem: west -> CMake configure -> devicetree
-> Kconfig -> geração de código -> compilação -> link (2 ou 3 passos) ->
pós-processamento. Duas delas surpreendem:

- **devicetree roda antes do Kconfig**, porque gera os símbolos
  `DT_HAS_<compat>_ENABLED` de que os drivers dependem;
- **o Zephyr linka mais de uma vez**, porque tabelas de interrupção só podem
  ser geradas depois que os endereços existem.

O caminho completo, fase por fase, está em [build-flow.md](build-flow.md).

**3. Alvos de placa.** `esp32c5_devkitc/esp32c5/hpcore` = placa / SoC / núcleo.
Esta placa tem dois núcleos (HP e LP), então o alvo precisa dizer qual. Sem os
qualificadores o build falha (e o erro lista os alvos válidos). Descubra com
`west boards | grep esp32c5`.

**4. O ciclo.** Editar -> `west build` -> `west flash` -> `west espressif monitor`.

---

## Lab 1 - hello_world (a vitória garantida)

Objetivo: provar que toolchain, gravação e console serial funcionam. Esta é a
âncora de confiança antes de qualquer conceito.

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore samples/hello_world
west flash
west espressif monitor
```

Esperado no console:

```
*** Booting Zephyr OS ... ***
Hello World! esp32c5_devkitc/esp32c5
```

Discussão enquanto compila:
- O que o `west build` juntou: CMake + Kconfig + devicetree -> um binário.
- O que o `west flash` fez: gravou o binário na placa via USB.
- O que o `west espressif monitor` faz: abre o console na velocidade certa,
  detectando a porta sozinho. Sai com `Ctrl+]`; force a porta com `-p <porta>`.
- A pegadinha da porta serial (UART0 vs USB-JTAG). Peça para todos confirmarem
  a porta agora, uma vez, para nunca mais atrapalhar.

Se o hello_world não aparecer: quase sempre é a porta serial errada ou um cabo
só de carga. Resolva aqui antes de seguir.

---

## Lab 2 - Devicetree e Kconfig

Esta é a hora difícil. Motivamos o devicetree batendo numa parede e escalando.

### 2a - Veja o build falhar

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore \
  samples/sensor/die_temp_polling
```

Falha com:

```
error: '__device_dts_ord_39' undeclared here
```

Este é **o erro mais comum do Zephyr**. Ele quer dizer: você pediu um
dispositivo que o build não criou.

### 2b - Por quê: o nó existe, mas está desligado

No devicetree do SoC:

```dts
coretemp: coretemp@6000e058 {
	compatible = "espressif,esp32-temp";
	status = "disabled";   /* <-- aqui */
};
```

O sensor **existe no silício** e está descrito no devicetree. Como está
`disabled`, nenhum device é criado - e o código que pede por ele não linka.

### 2c - O modelo mental em duas frases

- **Devicetree** descreve o *hardware*: o que existe e como está ligado (pinos,
  barramentos, endereços). São arquivos `.dts` / `.overlay`.
- **Kconfig** seleciona o *software*: quais subsistemas e drivers compilar. É o
  `prj.conf` e os símbolos `CONFIG_*`.

Regra de bolso: "é um fio ou um chip?" -> devicetree. "é um recurso ou driver
que quero ligar?" -> Kconfig.

### 2d - Ligue o nó com um overlay

Arquivo pronto:
`labs/lab2-devicetree/boards/esp32c5_devkitc_hpcore.overlay`

```dts
&coretemp {
	status = "okay";
};
```

Copie para `samples/sensor/die_temp_polling/boards/esp32c5_devkitc_hpcore.overlay`,
recompile e grave:

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore \
  samples/sensor/die_temp_polling
west flash
west espressif monitor
```

```
CPU Die temperature[coretemp]: 34.2 °C
```

Não mudamos **uma linha** do código do sample. Só descrevemos o hardware. E não
editamos nada do upstream: o `&coretemp` referencia um nó que já existe, e só
mudamos uma propriedade.

### 2e - A metade Kconfig

O `prj.conf` do sample tem `CONFIG_SENSOR=y`. Remova o
`CONFIG_SENSOR`, recompile pristine e veja quebrar (driver não compilado).
Coloque de volta. Devicetree disse que existe; Kconfig decide se o driver entra
no binário.

### 2f - Inspecione o que foi gerado (o superpoder)

```
build/zephyr/zephyr.dts   devicetree final, já mesclado
build/zephyr/.config      Kconfig final, já resolvido
```

- "Meu overlay pegou?" -> procure o nó em `zephyr.dts`. Se não está lá, o
  arquivo **não foi lido** (nome errado, ou pasta errada).
- "Esse CONFIG ficou ligado?" -> procure em `.config`, não no `prj.conf`.

Estes dois arquivos resolvem a maioria das dúvidas de devicetree e Kconfig.

Espiada opcional: `west build -t menuconfig` para navegar/buscar os `CONFIG_*`.

### 2g - O LED que a placa tem e o devicetree não descreve

No 2b o nó **existia e estava desligado**. Existe um caso pior, e a placa tem
um exemplo dele: o **LED RGB**.

Abra o `.dts` da placa e procure por LED. Não tem. O arquivo declara só
`sw0` e `watchdog0`:

```dts
aliases {
    sw0 = &boot_button;
    watchdog0 = &wdt0;
};
```

O LED existe no hardware, no **GPIO27**. O que falta é a *descrição*. Sem nó
não há `status` para ligar: alguém precisa **escrever o nó**.

**Aqui não é você quem escreve, e não há erro para corrigir.** Alguém já
escreveu esse nó, e o arquivo vem junto do sample na própria árvore do Zephyr.
Diferente do 2a, este build **funciona de primeira**:

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore samples/drivers/led/led_strip \
  -- -DCONFIG_SAMPLE_LED_UPDATE_DELAY=350
west flash
```

O LED acende e muda de cor. O `-D` depois do `--` sobrepõe um Kconfig do
sample sem editar arquivo nenhum: o default é 50 ms, e nesse ritmo o LED
pisca rápido demais para se ver a troca de cor.

A descrição está em
`samples/drivers/led/led_strip/boards/esp32c5_devkitc_hpcore.overlay`, e vale
abrir: ela cria o nó `ws2812@0`, define o alias `led-strip`, aponta o pinctrl
do I2S para o `GPIO27` e liga o DMA.

Três coisas que este lab deixa:

| | |
|---|---|
| Nó desligado (2b) | o nó existe, você muda `status` |
| Nó ausente (2g) | o nó não existe, você escreve o nó inteiro |
| Onde o overlay mora | não precisa ser seu: um sample pode trazer o dele |

E fecha a dúvida do Lab 1: o `blinky` não compila aqui não porque falte LED,
mas porque ele quer um `led0` de **GPIO**, e este é um WS2812 endereçável
(`led-strip`) acionado por I2S. Driver diferente, API diferente.


---

## Lab 3 - Botão, interrupção de GPIO e log

Objetivo: entrada de GPIO por interrupção (não por polling) e o subsistema de
log. Sem um `led0` de GPIO na placa, o console é o nosso dispositivo de saída
aqui.

### 3a - O sample do botão

A placa define `sw0` (o botão de boot, GPIO28). Este sample já o usa:

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore samples/basic/button
west flash
west espressif monitor
```

Aperte o botão; o console mostra os apertos. Destaque que o `button` usa uma
**interrupção + callback** de GPIO, não um laço ocupado (busy-loop).

### 3b - Sua versão, com log

Arquivos prontos em `labs/lab3-button/` (`main.c`, `prj.conf`,
`CMakeLists.txt`). Não precisa de overlay: o `sw0` já vem da placa.

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore labs/lab3-button
west flash
west espressif monitor
```

`prj.conf`:

```
CONFIG_GPIO=y
CONFIG_LOG=y
```

Pontos de fala: `printk` é uma impressão bloqueante e sem estrutura;
`LOG_INF/WRN/ERR` são adiados, com níveis e filtráveis por módulo. Iniciantes
devem usar as macros de log.

O `LOG_MODULE_REGISTER` no topo do `main.c` dá um **nome** a esse módulo de
log. Guarde isso: no Lab 6 esse nome vira o alvo do comando `log` no shell,
para ligar e desligar mensagens sem recompilar.

---

## Lab 4 - Threads e um semáforo

Objetivo: o modelo de execução do Zephyr - threads, dormir e uma primitiva de
sincronização.

Arquivos prontos em `labs/lab4-threads/`. Construção em etapas:

1. Uma thread de heartbeat loga a cada segundo com `k_msleep(1000)`. Mostre que
   `k_msleep` cede a CPU (escalonamento cooperativo/preemptivo), não é espera
   ocupada.
2. Uma segunda thread (worker) bloqueia num semáforo.
3. O botão (ISR) libera o semáforo e acorda a worker.

Trecho central (veja o `main.c` completo na pasta do lab):

```c
K_SEM_DEFINE(button_sem, 0, 1);

static void worker_thread(void *a, void *b, void *c)
{
	while (1) {
		k_sem_take(&button_sem, K_FOREVER);
		LOG_INF("worker woke up from the button ISR");
	}
}

K_THREAD_DEFINE(worker_id, 1024, worker_thread, NULL, NULL, NULL, 5, 0, 0);

/* in the button callback: */
k_sem_give(&button_sem);
```

Pontos de fala:
- `K_THREAD_DEFINE`: tamanho da pilha, entrada, prioridade - as três coisas que
  o iniciante precisa entender.
- Nunca bloqueie numa ISR; o callback só dá `k_sem_give` e retorna. A thread
  trabalhadora faz o trabalho lento. Esse é o padrão ISR-para-thread.
- Prioridades: número menor = prioridade maior.
- Depois, no shell: `kernel thread list` mostra as threads vivas.

---

## Lab 5 - Um comando de shell customizado

Objetivo: o subsistema de shell, um momento "isso é legal" que custa pouco.

Arquivos prontos em `labs/lab5-shell/` (inclui o overlay do
`coretemp`).

`prj.conf`:

```
CONFIG_SENSOR=y
CONFIG_SHELL=y
```

Registre um comando que lê o sensor:

```c
static int cmd_temp(const struct shell *sh, size_t argc, char **argv)
{
	struct sensor_value val;

	sensor_sample_fetch(temp_dev);
	sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &val);
	shell_print(sh, "die temperature: %d.%02d C", val.val1, val.val2 / 10000);
	return 0;
}

SHELL_CMD_REGISTER(temp, NULL, "Read the SoC die temperature", cmd_temp);
```

Compile, grave, abra o console, digite `temp` e `help`. O console já está ligado
ao shell (`zephyr,shell-uart = &uart0`). Deixe as pessoas explorarem comandos
embutidos (`kernel thread list`, `device list`) por um minuto - isso faz o RTOS
parecer real.

---

## Lab 6 - Tudo junto numa aplicação só

Objetivo: uma aplicação só, com todos os conceitos do dia.

Arquivos prontos em `labs/lab6-sensor/`.

O que ela combina:

| Conceito | No código |
|---|---|
| Devicetree | o sensor ligado pelo seu overlay |
| Kconfig | sensor, shell e log ligados |
| Threads | uma thread amostra a cada segundo |
| Sincronização | um mutex protege as estatísticas |
| Interrupção | o botão força uma leitura imediata |
| Shell | `temp` e `temp stats` |

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore labs/lab6-sensor
west flash
west espressif monitor
```

No console:

```
uart:~$ temp
die temperature: 34.85 C

uart:~$ temp stats
samples: 128
min: 33.90 C
max: 36.15 C
```

Aperte o botão: a thread acorda na hora e loga uma leitura extra.

### 6b - Controle o log sem recompilar

`CONFIG_LOG_CMDS=y` no `prj.conf` traz o comando `log` para o shell. Log não é
`printf`: cada módulo se registra com um nome (aqui, `tempmon`, definido por
`LOG_MODULE_REGISTER`) e o nível de cada um muda **em runtime**.

```
uart:~$ log status
uart:~$ log disable tempmon
uart:~$ log enable dbg tempmon
```

Aperte o botão com o log desligado e ligado, e compare. Os quatro níveis são
`err`, `wrn`, `inf` e `dbg`; `LOG_DBG` só existe no binário se o nível
compilado permitir (`CONFIG_LOG_DEFAULT_LEVEL`), então o controle em runtime
só desce até onde a compilação deixou.

É isso que separa log de `printk` num produto: você deixa a instrumentação no
código e liga só o módulo que está investigando, na máquina do cliente.

---

## Lab 7 - Wi-Fi: do scan até a internet

O rádio principal do C5, sem precisar de nenhum componente externo. Requer os
blobs (`west blobs fetch hal_espressif`, feito no pré-work).

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore samples/net/wifi/shell
west flash
west espressif monitor
```

O caminho completo são quatro comandos, e cada um responde uma pergunta.

**1. Quais redes existem?**

```
wifi scan
```

O scan é assíncrono: o comando responde `Scan requested` na hora e a tabela de
redes chega alguns segundos depois, fechando com `Scan request done`. Se você
pedir outro scan antes disso, aparece `Scan request failed` - é só o segundo
pedido sendo recusado, e os resultados do primeiro chegam normalmente.

**2. Entrar numa delas.**

```
wifi connect -s "NOME_DA_REDE" -k 1 -p "senha"
```

`-k` é o tipo de segurança: `1` é WPA2-PSK, que cobre quase toda rede
doméstica e hotspot de celular. Para uma rede **aberta**, use `-k 0` e não
passe `-p`. Se o nome da rede tiver espaço, as aspas são obrigatórias.

**3. Conectou mesmo? Qual o IP?**

```
wifi status
```

O endereço vem por DHCP, que já está ligado neste sample
(`CONFIG_NET_DHCPV4=y`). Se o `status` mostrar associado mas sem IP, espere uns
segundos: a associação acontece antes do DHCP terminar.

**4. Alcança a internet?**

```
net ping -c 3 1.1.1.1
```

Três respostas com `icmp_seq` e `ttl` significam que a pilha de rede está
completa: rádio, associação, DHCP, roteamento e ICMP.

### Se o ping falhar mas o `wifi status` estiver ok

Não é a placa. Quase sempre é a rede do local:

- **Portal cativo.** Redes de evento e de hotel associam e dão IP, mas seguram
  todo o tráfego até você aceitar os termos numa página. A placa não tem
  navegador, então o ping nunca passa.
- **ICMP bloqueado.** Muita rede corporativa e de visitante descarta ping.

Nos dois casos, a saída é um **hotspot de celular**, que não tem
portal cativo e não bloqueia ICMP.

---

## Lab 8 - BLE: a placa aparece no seu celular

Objetivo: transmitir a temperatura do chip no anúncio BLE e ler no celular.
O segundo dispositivo é o seu telefone, então continua sem hardware extra.

Arquivos prontos em `labs/lab8-ble/`.

Antes, instale um scanner BLE no celular: **nRF Connect** (Nordic) ou
**LightBlue** servem, os dois são gratuitos.

```bash
west build -p -b esp32c5_devkitc/esp32c5/hpcore labs/lab8-ble
west flash
west espressif monitor
```

No console:

```
[00:00:00.583,000] <inf> ble_temp: advertising as Espressif-c3d6
[00:00:01.412,000] <inf> ble_temp: broadcasting 34.85 C
```

O nome anunciado é o `CONFIG_BT_DEVICE_NAME` do `prj.conf` com os **dois
últimos bytes do endereço BLE** colados no fim, então por padrão sai algo como
`Espressif-c3d6` e nenhuma placa da sala fica igual a outra.

**Ponha o seu nome antes de compilar:**

```
CONFIG_BT_DEVICE_NAME="seu-nome"
```

Você vira `seu-nome-c3d6` no painel projetado. Nada de código muda - só um
símbolo de Kconfig, e o sufixo continua garantindo que dois xarás se distingam.

Procure o seu nome na lista. Toque no dispositivo e olhe o campo
**Manufacturer data**: quatro bytes.

```
FF FF 9D 0D
└──┬──┘ └──┬──┘
   │       └── temperatura em centésimos de grau, little-endian
   │           0x0D9D = 3485 -> 34.85 C
   └── company ID 0xFFFF, reservado para testes
```

Segure o dedo no chip por alguns segundos e veja o número subir no celular.

### O que este lab ensina

| Conceito | Onde |
|---|---|
| Anúncio é um payload que **você** monta | o vetor `ad[]` em `main.c` |
| Não precisa conexão nem pareamento | `BT_LE_ADV_NCONN_IDENTITY` |
| Atualizar o anúncio em runtime | `bt_le_adv_update_data()` |
| Byte order importa fora do chip | `sys_cpu_to_le16()` |
| Nome = configuração + identidade do rádio | `CONFIG_BT_DEVICE_NAME` + `bt_id_get()` |

O anúncio BLE cabe em **31 bytes**. Aqui vão 3 de flags, 6 de manufacturer
data e o resto de nome - por isso o nome é curto.

### Wi-Fi ou BLE?

Você acabou de usar os dois rádios da placa para a mesma tarefa, e a escolha
entre eles é uma decisão de produto:

| | Wi-Fi (Lab 7) | BLE (Lab 8) |
|---|---|---|
| Chega na internet | sim | não, precisa de um gateway |
| Consumo | alto | baixo |
| Precisa de infraestrutura | roteador e senha | nada, só um celular perto |
| Tempo até o primeiro dado | segundos (associação + DHCP) | imediato |

---

## Depois do workshop

**Não deixe seu projeto dentro de `zephyr/samples/`** - ele some no próximo
`west update`. Crie a pasta onde quiser, com os quatro arquivos da anatomia:

```bash
mkdir -p ~/meus-projetos/minha-app
# CMakeLists.txt, prj.conf, src/main.c, boards/*.overlay
west build -p -b esp32c5_devkitc/esp32c5/hpcore ~/meus-projetos/minha-app
```

Caixa de ferramentas:

| west | depuração |
|---|---|
| `west build` compilar | `LOG_LEVEL` filtrar por módulo |
| `west flash` gravar, `west espressif monitor` console | `device list` dispositivos prontos |
| `west debug` GDB + OpenOCD | `kernel thread list` pilhas e estados |
| `west boards` listar alvos | `build/zephyr/zephyr.dts` devicetree final |
| `west blobs` binários de RF | `build/zephyr/.config` Kconfig final |

---

## Lab 9 - Atualização OTA pela rede

O último lab troca o firmware **sem tocar na placa**, e desfaz se der errado.

Este lab usa o módulo `ota_http`, que vive em `modules/ota_http/` neste
repositório. Ele é um **módulo Zephyr de verdade**: é assim que código externo
entra num projeto seu.

### 9a - A placa entra na rede sozinha

Diferente do Lab 7, aqui o SSID e a senha vêm do `prj.conf`:

```
CONFIG_LAB9_WIFI_SSID="rede-da-sala"
CONFIG_LAB9_WIFI_PSK="senha"
```

No boot a aplicação conecta e imprime o endereço. Se a associação falhar, ela
tenta de novo: numa sala cheia de placas a primeira tentativa falha com
frequência.

### 9b - Trazendo o módulo pelo manifesto

Até agora o módulo entrou pela linha de comando:

```bash
# WS aponta para o repositório do workshop que você clonou
WS=~/zephyr-workshop-2026

west build -p -b esp32c5_devkitc/esp32c5/hpcore --sysbuild $WS/labs/lab9-ota \
  -- -DEXTRA_ZEPHYR_MODULES=$WS
```

O `EXTRA_ZEPHYR_MODULES` aponta para a **raiz** do repositório, não para
`modules/ota_http`: é lá que está o `zephyr/module.yml`.

Num projeto de verdade ele entra pelo `west.yml`. O arquivo
`labs/lab9-ota/west.yml.exemplo` mostra como. O detalhe que confunde: o west
clona o repositório **inteiro** no caminho indicado, e o Zephyr procura
`zephyr/module.yml` na **raiz** dele - por isso este repositório tem um
`zephyr/module.yml` apontando para `modules/ota_http`.

### 9c - Publicando a imagem

No computador do instrutor:

```bash
./scripts/publicar-ota.py build/lab9-ota
```

O script confere que a imagem é assinada, lê a versão do cabeçalho MCUboot,
copia para uma pasta servida e imprime a URL. O servidor é multi-thread, então
aguenta a sala inteira baixando ao mesmo tempo.

### 9d - O ciclo, e a parte que importa

```
uart:~$ ota download http://<servidor>:8000/lab9.signed.bin
  100%  (737452 / 737452 bytes)
uart:~$ ota apply          # sem "permanent": a imagem entra em teste
uart:~$ ota reboot         # a nova sobe, e avisa que esta em teste
uart:~$ ota status         # UNCONFIRMED
uart:~$ ota reboot         # sem confirmar -> o bootloader REVERTE
```

**Não confirmar é o caminho da reversão.** Firmware que não prova que funciona
volta sozinho, sem ninguém ir até o equipamento. É isso que torna atualização
em campo segura, e é o motivo de existirem dois slots.

Para manter a imagem nova, use `ota apply permanent`, ou `ota confirm` depois
que ela subir.

Detalhes do módulo, incluindo os modos de bootloader testados, estão em
[modules/ota_http/README.md](modules/ota_http/README.md).
