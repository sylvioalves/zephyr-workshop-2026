# ota_http

Módulo Zephyr que baixa um firmware por HTTP e escreve direto no slot que o
MCUboot não está executando. Depois um comando pede a troca no próximo boot.

É o equivalente prático do que outros SDKs oferecem numa chamada só: você aponta
para uma URL e o resto acontece.

## O que ele faz, e o que não faz

Faz: resolve a URL, abre o socket, faz o GET, e vai escrevendo o corpo da
resposta no slot secundário conforme os pedaços chegam. Não guarda a imagem em
RAM - o download é transmitido direto para a flash, então o tamanho do firmware
não é limitado pela memória.

Não faz: não verifica hash por conta própria. Isso é deliberado. Quem valida a
assinatura da imagem é o MCUboot, antes de trocar os slots, e é essa validação
que decide se a imagem chega a rodar. Uma checagem nossa antes disso daria uma
falsa sensação de garantia.

## Usando

O módulo é out-of-tree, então aponte para ele na build:

```bash
WS=~/zephyr-workshop-2026

west build -p -b esp32c5_devkitc/esp32c5/hpcore --sysbuild \
  $WS/modules/ota_http/samples/ota-http \
  -- -DEXTRA_ZEPHYR_MODULES=$WS/modules/ota_http
```

Para incorporar num produto, o caminho normal é declarar no seu `west.yml` em
vez de passar o caminho na linha de comando.

### Kconfig

| Símbolo | Padrão | Para quê |
|---|---|---|
| `OTA_HTTP` | n | liga o módulo |
| `OTA_HTTP_SHELL` | y | registra o comando `ota` |
| `OTA_HTTP_RECV_BUF_SIZE` | 2048 | buffer de recepção; maior significa menos escritas em flash |
| `OTA_HTTP_TIMEOUT_MS` | 60000 | vale para a transferência inteira, não por pedaço |
| `OTA_HTTP_CONFIRM_TIMEOUT_S` | 0 | segundos para confirmar uma imagem em teste antes de reverter sozinho |
| `OTA_HTTP_TLS` | n | aceita URLs https |
| `OTA_HTTP_TLS_SEC_TAG` | 1 | tag onde o certificado CA foi registrado |
| `OTA_HTTP_TLS_PEER_VERIFY` | 2 | 0 desliga, 1 opcional, 2 exige. Deixe em 2 em produto |

### Modos de bootloader, testados no C5

| Modo | Troca | Reverte | Imagens |
|---|---|---|---|
| `SWAP_USING_MOVE` | sim | sim | uma |
| `SWAP_USING_OFFSET` | sim | sim | uma |
| `DIRECT_XIP` | sim | sim | **uma por slot** |
| `OVERWRITE_ONLY` (padrão) | sim | **não** | uma |

O sample usa `SWAP_USING_MOVE`: é o mais maduro, usa uma imagem só e uma URL
só. O `SWAP_USING_OFFSET` é mais rápido e desgasta menos a flash; trocar é uma
linha no `sysbuild.conf`.

O **DIRECT_XIP** executa no lugar, sem copiar, e escolhe o slot pela versão da
imagem. Em troca exige uma imagem **linkada para cada slot**: os segmentos
mapeados (IROM/DROM) têm o offset gravado no link, então uma imagem de slot 0
rodando do slot 1 lê dados do slot errado. O `slot1/slot1.overlay` do sample é
a variante do slot 1, usada só nesse modo.

Nesse modo a placa também precisa baixar a variante do slot livre, e o comando
`ota download` hoje recebe uma URL fixa - quem escolhe é quem digita.

### O rollback depende de uma escolha do sysbuild

**O padrão do sysbuild é `SB_CONFIG_MCUBOOT_MODE_OVERWRITE_ONLY`, e nesse modo
não existe reversão** - o Kconfig do MCUboot diz isso com todas as letras. O
`sysbuild.conf` do sample sai desse padrão:

```
SB_CONFIG_MCUBOOT_MODE_SWAP_USING_MOVE=y
```

Sem essa linha os comandos `apply` e `confirm` continuam existindo, mas a
imagem antiga já foi sobrescrita e não há para onde voltar.

## O comando `ota`

```
ota download <url>      baixa para o slot secundário
ota apply [permanent]   pede a troca no próximo boot
ota confirm             confirma a imagem atual
ota status              diz se a imagem atual está confirmada
ota erase               apaga o slot secundário
ota reboot              reinicia a placa
```

## O ciclo que interessa

```
ota download http://192.168.0.10:8000/zephyr.signed.bin
ota apply                 # sem "permanent": a imagem entra em teste
ota reboot                # a imagem nova sobe
ota status                # diz que está NÃO confirmada
ota reboot                # sem confirmar -> o MCUboot reverte sozinho
```

Repetindo com `ota confirm` antes do segundo reboot, a imagem nova fica.

Esse é o mecanismo que torna atualização em campo segura: firmware que não prova
que funciona é revertido sem ninguém precisar ir até o equipamento.

## Servindo a imagem

Qualquer servidor HTTP serve. Para testar na bancada:

```bash
cd build/ota-http/zephyr && python3 -m http.server 8000 --bind 0.0.0.0
```

O arquivo a baixar é o `zephyr.signed.bin`, não o `zephyr.bin`. O MCUboot só
aceita a imagem assinada.

## Estado

Compila limpo para `esp32c5_devkitc/esp32c5/hpcore` com zero warnings.
**O ciclo de download e reversão ainda não foi executado em hardware.**
