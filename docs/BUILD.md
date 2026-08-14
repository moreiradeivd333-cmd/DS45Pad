# Compilação

## Pré-requisitos

Um plugin SPRX da VSH precisa do **toolchain PPU do Cell SDK**.

| Ferramenta | Observação |
|---|---|
| Cell SDK (PPU) | `ppu-lv2-gcc` no PATH, variável `CELL_SDK` apontando para a raiz |
| `scetool` | Assinatura do SPRX. Precisa das chaves de retail 3.55 |
| `make` | GNU make |

## Por que o PSL1GHT não serve (e essa pergunta merece resposta precisa)

O PSL1GHT é open source e tem, sim, **toda a API** que este plugin usa:
`ioPadLddDataInsert`, `ioPadLddRegisterController`, `usbRegisterExtraLdd`,
`usbInterruptTransfer`, `usbLddOps`, `lv2syscall4`, `sysFs*`. Olhando só a
lista de funções, dá a impressão de que um port resolveria.

Não resolve, e o motivo não é a API — é o **formato do artefato**.

Um app homebrew é um `.self`: um executável que o XMB carrega como processo
próprio. Um plugin da VSH é um **PRX**: um módulo relocável, com seção
`.sceModuleInfo` e pontos de entrada `module_start`/`module_stop`, injetado num
processo que **já está rodando** (`vsh.self`). São coisas diferentes.

Produzir um PRX exige o modo `-mprx` do compilador, que muda o modelo de código
(relocações e tratamento de TOC próprios) e emite a seção de módulo. Isso é uma
extensão da SCE ao GCC dela. O GCC do `ps3toolchain` é GCC padrão com patches de
newlib e **não tem `-mprx`**.

Verificando o PSL1GHT direto na fonte (`ppu_rules`, `base_rules`, e os forks do
bucanero, bigboss e kakaroto): as únicas regras de saída são `%.a`, `%.o`,
`%.elf`, `%.self` e `%.pkg`. **Não existe regra que produza `.prx`.** O
`sprxlinker`, apesar do nome, faz o contrário do que parece: ele corrige as
seções de *stub* de um ELF para que um homebrew consiga **chamar** as SPRX do
sistema. Ele consome SPRX, não gera.

É por isso que todo plugin da cena — PS3xPAD, webMAN MOD, Akari (que usa uma
solution do Visual Studio com o ProDG), RouLetteVshMenu — depende do SDK
proprietário. Não é preguiça da cena; é a única opção.

Em tese daria para forjar a seção de módulo na mão com linker script e `-fPIC`,
mas a mudança de ABI do `-mprx` faz disso um projeto de engenharia reversa por
si só, sem caso conhecido de sucesso.

O SDK é distribuído na cena, não pela Sony. O repositório não tem como resolver
esse passo por você.

## Compilando

O caminho testado é o `build.sh`, e é o que você deve usar:

```bash
cd src
./build.sh        # ds45pad_vsh.sprx
```

```bash
cd src
./build.sh game   # ds45pad_game.sprx
```

Ele não usa GNU make de propósito — veja [COMPILAR.md](COMPILAR.md) para o
motivo. Os dois builds **não** compartilham lista de fontes, de bibliotecas nem
auth-id, e o script verifica que a saída começa com `SCE` antes de terminar.

O `Makefile` continua no repositório para quem tem um ambiente SDK completo
(`make`, `make game`, `make all-builds`), com os mesmos alvos separados. Ele não
é o caminho verificado aqui: se `make` e `build.sh` divergirem, o `build.sh`
está certo.

O build de jogo é o mesmo código com `-DDS45_GAME`. Ele não é a versão "com
vibração" — vibração saiu do escopo. É o build que se injeta num jogo em
execução para corrigir a curva dos analógicos e responder ao
`cellPadGetInfo2` do jogo; veja [ARQUITETURA.md](ARQUITETURA.md).

## Assinatura

```
scetool -0 SELF -1 TRUE -s FALSE -2 0A -3 1070000052000001 \
        -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000 \
        -e ds45pad.prx ds45pad_vsh.sprx
```

- **`-2 0A`** — revisão de chave 3.55. **Não é opcional no HEN.** Um plugin
  assinado com revisão mais antiga faz o HEN travar no boot. É exatamente por
  isso que o PS3xPAD, que usa `-2 04`, teve que ser relançado como
  "3.55 signed for HEN". O webMAN MOD usa `0A` e carrega nos dois.
- `-3 1070000052000001` — *auth ID* de aplicativo, o que permite o plugin rodar
  no processo da VSH.
- `-6 0003004000000000` — firmware alvo, mantido baixo de propósito.
- `-A 0001000000000000` — versão do app.

Mexer nisso normalmente resulta num plugin que não carrega — ou, no HEN, num
console que não passa do boot. Veja o [guia de instalação no HEN](INSTALAR_HEN.md)
para a rede de segurança antes de testar.

## Instalação depois de compilar

```
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx     <- src/ds45pad_vsh.sprx
/dev_hdd0/plugins/ds45pad/ds45pad.txt          <- data/ds45pad.txt
/dev_hdd0/plugins/ds45pad/ds45pad_devices.txt  <- data/ds45pad_devices.txt
```

E a linha no carregador de plugins:

```
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx
```

`boot_plugins.txt` para Cobra, `plugins.txt` para o PRX Loader.

## Requisitos no console

- CFW com **Cobra/Mamba** habilitado, ou **HEN**.
- As syscalls 573/574 dão a conformidade total. Sem elas o plugin ainda carrega
  e funciona, só perde o bit `PS3_CONFORMITY` — o botão PS continua bom.
- Não precisa de kernel DEX nem de EBOOT debug para nada, **exceto** se você
  quiser carregar o `ds45pad_game.sprx` por métodos que exijam isso. Via
  PS3MAPI do webMAN MOD, retail serve.

## Se der erro de compilação

| Erro | Causa provável |
|---|---|
| `system_call_4` não declarado | O header de syscalls do SDK não veio junto; adicione `#include <sys/syscall.h>` em `padldd.c` e `main.c` |
| `CellUsbdLddOps` não declarado | `cell/usbd.h` ausente — SDK incompleto |
| `undefined reference to memcpy` | `libc.c` não entrou em `PPU_SRCS` |
| `cellPadGetRawData` não declarado | SDK antigo. É usado só pelo diagnóstico: apague as quatro linhas que o chamam em `diag.c` |
| `scetool: command not found` | Não está no PATH, ou faltam as chaves |
