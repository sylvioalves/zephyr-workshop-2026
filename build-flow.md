# O que acontece quando você roda `west build`

Referência detalhada do caminho completo, da linha de comando até o binário.
Tudo aqui foi verificado na árvore Zephyr **4.4.99** com um build real de
`samples/hello_world` para `esp32c5_devkitc/esp32c5/hpcore`.

> Este documento é material de **referência/aprofundamento**. No workshop de 4
> horas cobrimos só o resumo (fases 1 a 7 em um slide). Leia isto depois, com
> calma, quando quiser entender por que uma mudança sua "não pegou".

Resumo em uma linha:

```
west -> CMake configure -> devicetree -> Kconfig -> geração de código
     -> compilação -> link em vários passos -> pós-processamento
```

---

## Fase 0 - O comando `west build`

`west build` não é um binário: é uma **extensão** do west que vem da própria
árvore Zephyr, declarada no `west-commands.yml` e implementada em
`zephyr/scripts/west_commands/build.py`.

O que ele faz antes de qualquer coisa:

1. Descobre o **diretório de build** (`--build-dir` / `-d`, senão `build/`).
2. Descobre a **placa**, nesta ordem: `-b/--board` -> `BOARD` guardado no
   `build/CMakeCache.txt` de um build anterior -> variável de ambiente `BOARD`.
3. Descobre o **diretório da aplicação** (argumento posicional, senão o
   diretório atual).
4. Se você passou `-p` / `--pristine always`, **apaga o diretório de
   build inteiro**. É por isso que `-p` resolve tanta coisa: nada de cache.
5. Decide se precisa rodar o *configure* do CMake:
   - se não existe `build/CMakeCache.txt` -> roda o configure;
   - se já existe e nada mudou -> pula direto para o build.
6. Roda o gerador de build (Ninja por padrão).

Tudo o que vem depois de `--` na linha de comando é repassado ao CMake. Por
isso `-- -DEXTRA_DTC_OVERLAY_FILE=...` funciona.

---

## Fase 1 - CMake configure: o boilerplate

O `CMakeLists.txt` da sua aplicação tem exatamente uma linha que importa:

```cmake
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

Isso encontra `share/zephyr-package/cmake/ZephyrConfig.cmake`, que chama
`include_boilerplate()`, que inclui `cmake/modules/zephyr_default.cmake`. Esse
arquivo monta uma **lista ordenada de módulos CMake** e inclui um por um. A
ordem real, direto da árvore:

| # | Módulo | O que resolve |
|---|---|---|
| 1 | `python` | acha o interpretador Python correto |
| 2 | `user_cache` | cache do usuário (`~/.cache/zephyr`) |
| 3 | `extensions` | define as funções `zephyr_*()` usadas em todo lugar |
| 4 | `version` | versão do kernel -> `version.h` |
| 5 | `basic_settings` | configurações básicas de CMake |
| 6 | `west` | localiza o workspace west |
| 7 | `yaml` | leitura de YAML |
| 8 | `root` | resolve `BOARD_ROOT`, `SOC_ROOT`, `DTS_ROOT`, `MODULE_EXT_ROOT` |
| 9 | `zephyr_module` | lê o manifesto e registra **todos os módulos** |
| 10 | `boards` | resolve a placa e seus qualificadores |
| 11 | `shields` | resolve shields |
| 12 | `snippets` | resolve snippets |
| 13 | `hwm_v2` | modelo de hardware v2 (placa/SoC/núcleo) |
| 14 | `configuration_files` | descobre `prj.conf`, overlays, arquivos extras |
| 15 | `generated_file_directories` | cria `build/zephyr/include/generated/` |
| 16 | `dts` | **devicetree** (fase 2) |
| 17 | `kconfig` | **Kconfig** (fase 3) |
| 18 | `arch` | seleciona a arquitetura |
| 19 | `soc` | seleciona o SoC |
| - | `kernel` | por último: monta os alvos de build |

Dois pontos que explicam muita coisa:

- **`boards` roda antes de `dts`.** É aqui que o alvo
  `esp32c5_devkitc/esp32c5/hpcore` é validado. Se os qualificadores estiverem
  errados, o build morre nesta fase, antes de olhar qualquer devicetree - e o
  erro lista os alvos válidos.
- **`dts` roda antes de `kconfig`.** Não é detalhe de implementação: é uma
  dependência real, explicada na fase 3.

---

## Fase 2 - Devicetree

Implementado em `cmake/modules/dts.cmake`.

### 2.1 Coleta das fontes

O Zephyr monta **um único** arquivo `.dts` concatenando, nesta ordem:

1. o `.dtsi` do SoC (ex.: `dts/riscv/espressif/esp32c5/esp32c5_common.dtsi`);
2. o `.dts` da placa (ex.: `esp32c5_devkitc_hpcore.dts`);
3. os `.dtsi` de shields, se houver;
4. os overlays de snippets;
5. os **seus overlays**: `boards/<alvo>.overlay` da aplicação e tudo que vier
   em `EXTRA_DTC_OVERLAY_FILE`.

O último a falar vence. É assim que um overlay de 3 linhas muda uma propriedade
de um nó definido lá no SoC.

> É exatamente aqui que o nome do arquivo importa. Se o overlay se chama
> `esp32c5_devkitc.overlay` mas o alvo é `esp32c5_devkitc/esp32c5/hpcore`, o
> arquivo simplesmente **não entra na lista** - e o build passa sem ele.

### 2.2 Pré-processamento

O conjunto passa pelo **pré-processador C** (o mesmo do compilador). É por isso
que `#include`, `#define` e macros como `GPIO_ACTIVE_HIGH` funcionam dentro de
um `.dts`.

Saídas: `build/zephyr/zephyr.dts.pre` (resultado do preprocessador) e
`build/zephyr/zephyr.dts.d` (lista de dependências, para o CMake saber quando
refazer).

### 2.3 Parsing, validação e geração

Roda `scripts/dts/gen_defines.py`, que usa a biblioteca `python-devicetree`:

- monta a árvore completa (EDT - *extended device tree*);
- para cada nó, procura o **binding** correspondente ao `compatible` em
  `dts/bindings/`;
- **valida** as propriedades contra o binding (tipos, obrigatórias, `reg`,
  `interrupts`, etc.). Propriedade fora do binding vira aviso; binding ausente
  para um nó habilitado costuma virar erro mais adiante, no link.

Saídas geradas (todas verificadas no build real):

| Arquivo | Conteúdo |
|---|---|
| `build/zephyr/zephyr.dts` | **devicetree final, já mesclado** - a fonte da verdade |
| `build/zephyr/include/generated/zephyr/devicetree_generated.h` | todas as macros `DT_*` que seu código usa |
| `build/zephyr/edt.pickle` | a árvore serializada, reusada por outros scripts |
| `build/zephyr/dts_bindings_used.txt` | quais bindings foram usados |

### 2.4 `dtc` como linter opcional

Se o `dtc` estiver instalado, ele roda **apenas para gerar avisos extras**
(endereços duplicados, unit-address inconsistente). Ele não produz o binário do
devicetree: o Zephyr não usa DTB em runtime, tudo vira macro em tempo de
compilação.

### 2.5 A ponte para o Kconfig

Roda `scripts/dts/gen_driver_kconfig_dts.py`, que gera símbolos Kconfig do tipo:

```
DT_HAS_ESPRESSIF_ESP32_TEMP_ENABLED
```

um para cada `compatible` **presente e habilitado** no devicetree.

**É por isso que devicetree roda antes de Kconfig.** Drivers usam esses
símbolos como dependência:

```
config ESP32_TEMP
	bool "ESP32 Temperature Sensor"
	default y
	depends on DT_HAS_ESPRESSIF_ESP32_TEMP_ENABLED
```

Ou seja: habilitar o nó no devicetree é o que faz o driver aparecer no Kconfig.
Ligar o hardware liga o software.

---

## Fase 3 - Kconfig

Implementado em `cmake/modules/kconfig.cmake`, executado por
`scripts/kconfig/kconfig.py` (usa a biblioteca `kconfiglib`).

### 3.1 Ordem de mesclagem

Os fragmentos são aplicados nesta ordem - **o último vence**:

1. defconfig do SoC / arquitetura;
2. `Kconfig.defconfig` da placa e dos módulos (valores default condicionais);
3. **defconfig da placa** (`esp32c5_devkitc_hpcore_defconfig`);
4. fragmentos de snippets e shields;
5. **o seu `prj.conf`** (`CONF_FILE`);
6. **`EXTRA_CONF_FILE`** (fragmentos extras que você passa na linha de comando);
7. `-DCONFIG_*=...` direto na linha de comando.

Somado a isso, entram os símbolos `DT_HAS_*` vindos da fase 2.

### 3.2 Resolução e checagens

O kconfiglib resolve todo o grafo de dependências:

- um símbolo cujo `depends on` não é satisfeito **não é habilitado**, mesmo que
  você tenha escrito `=y` no `prj.conf`. Isso gera um **aviso**, não um erro -
  e é a causa número um de "eu liguei o CONFIG e não aconteceu nada";
- `select` força símbolos dependentes;
- ranges e tipos são validados.

### 3.3 Saídas

| Arquivo | Uso |
|---|---|
| `build/zephyr/.config` | **configuração final resolvida** - consulte sempre este, nunca o `prj.conf` |
| `build/zephyr/include/generated/zephyr/autoconf.h` | os `CONFIG_*` como `#define`, incluído em todo `.c` |
| `build/zephyr/kconfig/sources.txt` | quais arquivos Kconfig foram lidos |

---

## Fase 4 - Geração de código

Antes de compilar, vários scripts Python geram fontes e cabeçalhos. Do build
real de `hello_world`:

| Gerado | Por | Para quê |
|---|---|---|
| `zephyr/syscalls/*.h` (92 arquivos) | `gen_syscalls.py` | stubs de chamadas de sistema |
| `syscall_list.h`, `syscall_dispatch.c` | `gen_syscalls.py` | tabela de dispatch |
| `offsets.h` | `gen_offset_header.py` | offsets de structs para o assembly |
| `driver-validation.h` | `gen_kobject_list.py` | checagem de tipo de device em tempo de compilação |
| `kobj-types-enum.h`, `otype-to-*.h` | `gen_kobject_list.py` | tipos de objetos do kernel |
| `version.h` | módulo `version` | versão do kernel |
| `linker.cmd` | pré-processador C sobre o `linker.ld` do arch/SoC | script de link |

O `linker.ld` também passa pelo pré-processador: por isso ele enxerga
`CONFIG_*` e o layout muda conforme a sua configuração.

---

## Fase 5 - Compilação

- O toolchain vem do **Zephyr SDK** (aqui `riscv64-zephyr-elf-gcc`), escolhido
  pelos módulos `arch`/`soc`.
- Cada subsistema, driver e módulo vira uma **biblioteca estática** própria; o
  kernel e o resto entram em `libzephyr.a`; o seu código vira a lib `app`.
- Todo `.c` recebe `-imacros .../autoconf.h`: é assim que os `CONFIG_*` chegam
  ao código sem ninguém incluir nada.

---

## Fase 6 - Link em vários passos (a parte que surpreende)

O Zephyr **não linka uma vez só**. A própria `CMakeLists.txt` do Zephyr define
três estágios:

| Estágio | Quando existe | Estado |
|---|---|---|
| `zephyr_pre0` | sempre | seções ainda podem mudar de tamanho, endereços podem se mover |
| `zephyr_pre1` | só com `CONFIG_USERSPACE` ou `CONFIG_DEVICE_DEPS` | tamanhos já fixos, endereços não mudam mais |
| `zephyr_final` | sempre | imagem final |

Por que mais de um passo? Porque algumas tabelas **só podem ser geradas depois
de saber os endereços**, e gerá-las muda o binário:

- **tabelas de interrupção**: `gen_isr_tables.py` lê o ELF pré-construído e
  gera `isr_tables.c`, `isr_tables_vt.ld`, `isr_tables_swi.ld`;
- **hash de objetos do kernel** (`CONFIG_USERSPACE=y`): `gen_kobject_list.py`
  gera uma tabela gperf a partir dos objetos encontrados no ELF;
- **partições de memória da aplicação** (`CONFIG_USERSPACE=y`);
- **dependências de devices** (`CONFIG_DEVICE_DEPS=y`).

No nosso build de `hello_world` (sem userspace) saíram exatamente dois ELFs -
verificado:

```
build/zephyr/zephyr_pre0.elf
build/zephyr/zephyr.elf
```

E dois scripts de link: `linker_zephyr_pre0.cmd` e `linker.cmd`.

---

## Fase 7 - Pós-processamento e saídas

- `objcopy` gera `zephyr.bin` e `zephyr.hex` a partir do `zephyr.elf`;
- `zephyr.map` / `zephyr_final.map`: mapa de memória (onde cada símbolo foi
  parar, e quanto cada coisa ocupa);
- `runners.yaml`: **como** o `west flash` vai gravar - qual runner (esptool,
  OpenOCD), quais argumentos, offsets de partição;
- em Espressif, o binário ainda recebe cabeçalho/padding no formato que o
  bootloader espera.

---

## Colinha de depuração

Quando algo "não pegou", a resposta quase sempre está em um destes:

| Pergunta | Onde olhar |
|---|---|
| Meu overlay foi aplicado? | `build/zephyr/zephyr.dts` - procure o nó |
| Esse `CONFIG_` ficou ligado? | `build/zephyr/.config` (nunca o `prj.conf`) |
| Qual macro `DT_*` existe para o meu nó? | `include/generated/zephyr/devicetree_generated.h` |
| Quais arquivos Kconfig entraram? | `build/zephyr/kconfig/sources.txt` |
| Quais bindings foram usados? | `build/zephyr/dts_bindings_used.txt` |
| Por que a imagem ficou tão grande? | `build/zephyr/zephyr.map` |
| Como o flash vai ser feito? | `build/zephyr/runners.yaml` |

E a regra de ouro: mudou placa, overlay ou Kconfig -> `west build -p`.
As fases 2 e 3 dependem de cache, e um build sujo mente para você.
