# DS45PAD

> 🇬🇧 [Read in English](README.md) — the main README.

Plugin para PlayStation 3 (CFW ou HEN) que faz **DualShock 4** e **DualSense**
funcionarem em jogos que exigem controle oficial, e corrige a resposta dos
analógicos para igualar a de um DualShock 3.

Testado e funcionando em PS3HEN com DualSense e God of War II HD.

---

## Os dois problemas, e como cada um é resolvido

### 1. Jogos que exigem DualShock 3

Esses jogos leem `cellPadGetInfo2()` e checam `device_type` e os bits de
capacidade. Um pad virtual **nunca** satisfaz isso: ele sempre reporta
`CELL_PAD_DEV_TYPE_LDD` (5), nunca `STANDARD` (0).

Adaptadores de hardware passam porque **são** um DualShock 3 aos olhos do
console — VID `054C:0268`, descritor HID e formato de relatório do DS3. Nenhum
plugin pode fazer isso; não se muda o que um dispositivo USB diz de si mesmo.

Mas de dentro do processo do jogo existe uma terceira via: **responder a
pergunta**. O `ds45pad_game.sprx` intercepta `cellPadGetInfo2` e informa
`device_type = 0` com conformidade total. O jogo aceita o controle que estiver
plugado — sem adaptador.

### 2. Analógicos errados nos extremos

Não é erro de mapeamento, é curva de resposta. O conversor do DualShock 3
**satura a ~80% do curso mecânico**: uma diagonal cheia dá `0x0F`. O
DS4/DualSense é linear até o batente e dá `0x26` na mesma diagonal. Hall effect
piora, com faixa reduzida (muitos só vão de `0x14` a `0xEB`).

O mesmo hook corrige os valores em trânsito. Resultado medido, diagonal cheia:

| | sem correção | com correção | DualShock 3 real |
|---|---|---|---|
| DS4/DualSense de fábrica | `0x25` | **`0x0E`** | `0x0F` |
| Hall effect | `0x25` | **`0x0F`** | `0x0F` |

Depois de calibrado, Hall effect entrega os mesmos números que um stick de
fábrica. Detalhes em [docs/ANALOGICOS.md](docs/ANALOGICOS.md).

---

## Arquitetura

Dois módulos, papéis diferentes:

| Módulo | Onde roda | O que faz |
|---|---|---|
| **`ds45pad_game.sprx`** | Dentro do processo do jogo | Compatibilidade + curva dos analógicos. **É o que importa.** |
| `ds45pad_vsh.sprx` | Processo da VSH | Opcional: botão PS no XMB, via pad virtual LDD |

O hook funciona sobre **qualquer** controle que chegue ao jogo — DualSense
genérico, DS4, adaptador, DualShock 3 real — porque nunca toca em identidade de
dispositivo.

Como o desvio é feito: em PowerPC64 ELFv1 um ponteiro de função é o endereço de
um descritor `{ponto de entrada, TOC}`. Sobrescrever essas duas palavras
redireciona **todo** o processo. Medido no console: o descritor fica ~1,6 MB
fora do meu módulo, ou seja é o compartilhado dentro do `sys_io`.

---

## O que funciona

| Recurso | Estado |
|---|---|
| Jogos que exigem DualShock 3 | ✅ |
| Curva de analógico igual à do DS3 | ✅ |
| Calibração automática (Hall effect) | ✅ |
| Botão PS no XMB | ✅ com o módulo da VSH |
| Botão PS **em jogo** | ❌ estrutural — é a VSH que trata, não o jogo |
| Vibração, sixaxis, touchpad | ❌ não implementado por este caminho |

Para sair de um jogo sem botão PS: webMAN no navegador → aba PS3MAPI → **Exit**.

---

## Instalação

Precisa do **webMAN MOD**, que é quem carrega o módulo no processo do jogo.

- **PS3HEN:** [docs/INSTALAR_HEN.md](docs/INSTALAR_HEN.md)
- **CFW:** [docs/INSTALAR_CFW.md](docs/INSTALAR_CFW.md)

Resumo: `ds45pad_game.sprx` e `ds45pad.txt` em `/dev_hdd0/plugins/ds45pad/`,
webMAN no `boot_plugins.txt`, e com o jogo rodando carregue o módulo pela aba
PS3MAPI.

---

## Configuração

[`data/ds45pad.txt`](data/ds45pad.txt). O que importa nesta arquitetura:

| Chave | Padrão | Para quê |
|---|---|---|
| `STICK_SATURATION` | `80` | A curva. `80` = DS3; baixe para `75`/`71` se faltar alcance nos cantos |
| `STICK_DEADZONE` | `4` | Analógico andando sozinho. Hall effect: tente `0` |
| `STICK_CALIBRATION` | `AUTO` | Aprende o curso real do stick |
| `SPOOF_PAD_INFO` | `1` | Responde como DS3 conformante. Desligue se um jogo agir estranho |
| `INSERT_INTO_GAME` | `0` | **Deixe 0** — ligado, impede apps de carregar |

---

## Documentação

- [docs/ANALOGICOS.md](docs/ANALOGICOS.md) — a curva, Hall effect, calibração
- [docs/ARQUITETURA.md](docs/ARQUITETURA.md) — como funciona por dentro
- [docs/INSTALAR_HEN.md](docs/INSTALAR_HEN.md) — instalação em PS3HEN
- [docs/INSTALAR_CFW.md](docs/INSTALAR_CFW.md) — instalação em CFW
- [docs/COMPILAR.md](docs/COMPILAR.md) — ambiente de build
- [docs/DIAGNOSTICO.md](docs/DIAGNOSTICO.md) — boot trace e diagnóstico
- [docs/PESQUISA.md](docs/PESQUISA.md) — o que já existia, e o que aprendemos medindo

## Licença

GPLv2 — veja [LICENSE](LICENSE).
