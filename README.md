# Controle da Arvore de Natal - ESP32-C3 Super Mini

Projeto independente baseado nos padroes do
`Alarme_ESP32_v1.6.9_24102026_estavel_md4` (nova referencia, substituindo o v1.6.4).
Usa PlatformIO, framework Arduino, WiFiManager, NTPClient, WebServer, ArduinoJson e painel
local em SPIFFS. Os arquivos do projeto de alarme nao foram alterados.

## Funcoes

- Controle independente dos quatro reles, com nome editavel.
- Modos **Automatico**, **Ligar** e **Desligar** por rele.
- Indicador e botao de ligar em vermelho; indicador e botao de desligar em verde.
  Sem conexao, o indicador fica neutro e os comandos ficam desabilitados.
- Ate 16 periodos por rele, combinando repeticao semanal e mensal.
- Selecao de varios dias da semana ou de um dia do mes em cada periodo.
- Horarios de inicio e fim, inclusive atravessando a meia-noite.
- Pagina responsiva em portugues, sem CDN nem acesso externo.
- Relogio NTP e ajuste manual pela hora do navegador.
- Fuso configuravel; padrao Brasilia, UTC-3 (`-180` minutos).
- Agendas, nomes, fuso e senha OTA LAN persistidos na NVS.
- Acesso Web: usuario `admin` e senha definida na configuracao local.
- Falhas de comunicacao e de gravacao exibidas no painel.
- Diagnostico e ultimos 40 eventos em `/diag`, seguindo o modelo v1.6.9.
- Mensagens de eventos na ultima linha do LCD por 3 segundos.

Nao utiliza sensores de alarme, pulsos, buzina ou aplicativo Android do
projeto original. Os reles deste projeto sao saidas de estado continuo.
Primeira instalacao por USB; atualizacoes posteriores por OTA Web ou ArduinoOTA.

## Hardware

Placa alvo: [**ESP32-C3 Super Mini de 4 MB**](https://pt.aliexpress.com/item/1005007073915842.html) encaixada na
[eletechsup ESC3E05 de quatro reles](https://pt.aliexpress.com/item/1005013009987677.html).
Os reles seguem as ligacoes fixas do [diagrama da ESC3E05](https://ae-pic-a1.aliexpress-media.com/kf/S1ac564947dee40b38c21d60664f25d0cy.jpg).
Confira a serigrafia, a revisao e a orientacao do encaixe antes de ligar.

| Circuito | GPIO | Ligacao na ESC3E05 |
| --- | ---: | --- |
| CH1 / Rele 1 | 5 | Ligacao interna |
| CH2 / Rele 2 | 6 | Ligacao interna |
| CH3 / Rele 3 | 7 | Ligacao interna |
| CH4 / Rele 4 | 10 | Ligacao interna |
| LCD 2004 I2C SDA | 0 | Borne D0 |
| LCD 2004 I2C SCL | 1 | Borne D1 |
| ACK / consulta LCD | 3 | Borne D3, botao NA para GND |
| Reset de Wi-Fi opcional | 4 | Borne D4, botao NA para GND por 8 segundos |

### LCD 2004 I2C

Usa `LiquidCrystal_I2C` com SDA em D0 e SCL em D1. No boot procura o LCD nos
enderecos `0x23`, `0x27` e `0x3F`; conecte somente um LCD e reserve esses
enderecos para ele. O barramento I2C nao identifica o tipo do dispositivo:
uma resposta confirma apenas que existe um dispositivo naquele endereco.
Se nenhum ou mais de um endereco responder, ou houver erro no barramento, o LCD
nao e inicializado; o motivo aparece no serial e em `/diag`. O endereco detectado
aparece no serial, na tela inicial e em `/api/diag`. A busca ocorre apenas no
boot: desligue a alimentacao antes de substituir o LCD e reinicie o equipamento.

O tamanho **nao e detectado automaticamente**: este hardware nao informa suas
dimensoes via I2C. O firmware permanece configurado para LCD 2004 (20 colunas e
4 linhas), e requer um backpack compativel com a biblioteca utilizada.
O display apresenta:

1. Nome do projeto e data local (versao enquanto aguarda sincronizacao).
2. IP ou aviso de Wi-Fi desconectado.
3. Hora local, ou pedido de sincronizacao.
4. Estado dos quatro canais, por exemplo `1:LA 2:DA 3:LM 4:DM`:
   `L` ligado, `D` desligado, `A` automatico, `M` manual.

No portal, o serial informa o IP `192.168.4.1`; o LCD segue o v1.6.9: aviso de configuracao,
instrucao para conectar ao AP e SSID na ultima linha.
Erros de agenda/NVS substituem a ultima linha. Atualiza uma vez por segundo,
sem limpar a tela a cada ciclo. O backlight segue a politica de eventos do
modelo: 15 segundos ao iniciar/conectar, aceso durante o portal e 8 segundos
apos comandos web ou mudanca de estado de um rele. Consultas automaticas de
status nao prolongam a iluminacao.

O botao `ACK_BUTTON_PIN` no borne D3 acende o backlight por 8 segundos e exibe
imediatamente o estado dos quatro reles, encerrando mensagens temporarias.
Nao altera saidas, modos nem agendas. Possui debounce de 50 ms e registra
um evento por pressionamento; manter pressionado nao repete a acao.
No portal Wi-Fi ou durante OTA, preserva as instrucoes/progresso da tela.
Erros de NVS/agenda continuam tendo prioridade sobre o resumo dos canais.

Se o LCD nao responder no boot, o erro aparece no serial e no painel; o
controle web continua operando. Confira a ligacao e reinicie.
Ligue GND em comum e alimente o backpack conforme sua especificacao.
**Os GPIOs do ESP32 nao toleram 5 V.** Backpacks alimentados em 5 V podem ter
pull-ups I2C para 5 V: use conversor de nivel bidirecional apropriado ou uma
interface comprovadamente compativel com 3,3 V. Nao ligue SDA/SCL de 5 V
diretamente ao ESP32. Ajuste o trimpot de contraste do LCD se necessario.

O firmware continua configurado para reles ativos em HIGH. O diagrama da
ESC3E05 nao informa a polaridade; confirme-a sem cargas. Se a sua placa for
ativa em LOW, ajuste `RELAY_ON_LEVEL` de `1` para `0` em
[include/schedule.h](include/schedule.h) e grave novamente antes de ligar cargas.

**Teste primeiro sem cargas conectadas.** Confira cada CH e seu LED. Use os
contatos COM e NO quando a carga deva ficar desligada com o rele em repouso.
O firmware manda desligar os canais antes de inicializar rede e armazenamento,
mas nao controla os pinos durante o reset/bootloader. Verifique que os reles
nao pulsam ao energizar ou reiniciar a placa.

Deixe D2 e D8 livres: GPIO2 e GPIO8 sao pinos de strapping. O firmware nao
controla o LED integrado ligado ao GPIO8. GPIO9, GPIO20 e GPIO21 estao ligados ao RS485 da
ESC3E05 e nao sao usados pelo firmware; GPIO9 tambem atende ao BOOT.
GPIO18 e GPIO19 ficam reservados para o USB nativo. Nao aplique tensao
externa aos botoes.

Alimente a ESC3E05 pelos bornes VIN e GND com 7 a 25 V CC, conforme o
fabricante. Nao aplique essa tensao diretamente na Super Mini.
Instalacoes em 127/220 V devem ser realizadas por profissional habilitado, com
isolamento, caixa e protecoes apropriadas. A corrente impressa no rele nao
garante a capacidade do conjunto para qualquer carga; considere a corrente
de partida das fontes de iluminacao. Este nao e um controlador de seguranca.

## Compilar e gravar

Abra **esta pasta** no VS Code com a extensao PlatformIO IDE.
Antes de compilar, copie o modelo de credenciais:

```powershell
Copy-Item .\include\local_credentials.example.h .\include\local_credentials.h
```

Edite `include/local_credentials.h`, que nao e versionado. Defina senhas
distintas para `PANEL_PASSWORD` (painel e OTA Web, pelo menos 8 bytes) e
`AP_PASSWORD` (portal de configuracao Wi-Fi, de 8 a 63 bytes).
Nao envie o arquivo local nem o binario compilado com suas senhas.

Com o terminal nesta pasta:

```powershell
pio run
pio run --target buildfs
pio run --target uploadfs
pio run --target upload
pio device monitor --baud 115200
```

Se `pio` nao estiver no PATH, use:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

As tarefas em [.vscode/tasks.json](.vscode/tasks.json) incluem compilacao e
**PIO: Upload Completo (FS + FW)**. A gravacao completa envia primeiro o SPIFFS,
depois o firmware. Feche o monitor serial antes de gravar.

Se houver varias portas seriais, especifique `--upload-port COMx`.
O projeto compila no ambiente `esp32-c3-super-mini`, usando o perfil
`esp32-c3-devkitm-1` de 4 MB, USB CDC nativo e particoes `default.csv`
(dois slots OTA e SPIFFS). Confira o modulo e a placa antes de gravar. A mudanca de
chip e de layout exige gravar por USB o firmware e o SPIFFS deste projeto;
nao use OTA nem imagens do ESP32 anterior. Nao presuma que agendas ou senhas
existentes serao transferidas: faca uma copia antes de substituir a placa.

## Primeiro acesso

1. Grave SPIFFS e firmware e abra o monitor serial em 115200 baud.
2. O painel e o OTA Web usam o usuario `admin` e a senha `PANEL_PASSWORD`.
   A senha OTA LAN e aleatoria, separada e exibida no monitor serial.
3. Sem Wi-Fi salvo, conecte-se a rede `Arvore_Natal_ESP32_supermini` com `AP_PASSWORD`.
   Nao confunda com a senha do painel.
4. O portal WiFiManager abre a configuracao da rede de casa. Se necessario,
   acesse `http://192.168.4.1`. Essa etapa e um portal de rede, nao o painel.
5. Depois de conectar, abra o IP informado no serial. Quando suportado pela rede,
   tambem pode usar `http://arvore-natal.local`.
6. Assim como no modelo, o portal tem timeout de 120 segundos e funciona em
   modo nao bloqueante com `wifiManager.process()`. Se nao conectar em
   120 segundos de espera, o ESP32 reinicia e tenta novamente. Nao existe
   painel de controle em AP como fallback nesta configuracao. Durante o portal,
   `http://192.168.4.1` configura somente a rede Wi-Fi.
7. Entre com o usuario `admin` e a senha do painel.
8. Confira o relogio e o fuso. Em uma rede local sem Internet, clique em
   **Usar hora deste navegador**.
9. Expanda **Nome e agendamentos**, programe os periodos e clique em
   **Salvar este rele** em cada circuito.

A rede da casa pode ser configurada novamente reiniciando sem conexao valida,
ou acionando o reset de Wi-Fi opcional por 8 segundos. O reset de Wi-Fi nao
apaga agendas nem senha. A senha pode ser consultada novamente pelo serial
apos reiniciar; proteja o acesso fisico ao equipamento.

O ESP32 opera em modo estacao
(`WIFI_STA`) fora do portal e tenta reconectar automaticamente se perder a
rede durante a operacao. Os reles ficam desligados durante a configuracao
inicial. Apos a inicializacao, perder Wi-Fi nao interrompe o relogio nem as
agendas em andamento. Evite configurar simultaneamente outra placa com o
mesmo SSID do projeto modelo para nao confundir os portais.

## Como funcionam as agendas

Exemplo semanal: marque segunda a domingo e programe `18:00` -> `23:30`.
Exemplo noturno: sexta, `22:00` -> `02:00`: liga na sexta e desliga no sabado.
Exemplo mensal: dia `24`, `18:00` -> `01:00`: termina na madrugada do dia 25.

- Inicio inclusivo e fim exclusivo, com precisao de um minuto.
- Se o fim for menor que o inicio, o periodo termina no dia seguinte.
- Inicio igual ao fim e rejeitado, nao significa 24 horas.
- Varios periodos podem coexistir; a saida fica ligada enquanto **qualquer**
  periodo estiver ativo, sem prioridade entre semanal e mensal.
- Uma regra mensal repete todo mes, nao e um evento unico em uma data.
- Dia 31 nao inicia em mes com 30 dias; dia 29 de fevereiro so inicia em ano bissexto.
- Uma janela iniciada no ultimo dia valido continua no dia seguinte, mesmo
  na virada do mes/ano.
- Sem agenda ou sem periodo ativo, a saida automatica fica desligada.
- Periodo desmarcado como Ativo e ignorado, mas continua salvo.
- Salvar a agenda nao tira o rele do modo manual.
- **Ligar/Desligar sao estados fixos**, nao pulsos: suspendem a agenda daquele
  rele ate selecionar Automatico ou reiniciar.
- Modos manuais nao ficam salvos. No boot todos voltam a Automatico, com
  saidas desligadas ate o relogio ser valido. Depois, o periodo vigente e
  aplicado, inclusive quando a inicializacao acontece no meio de uma janela.

O relogio usa UTC internamente. O ajuste do navegador nao muda o fuso salvo.
Sem RTC com bateria, desligar o ESP32 perde a hora; e necessario novo NTP ou
ajuste pelo navegador no proximo boot. Depois de sincronizado, o relogio continua
funcionando sem Wi-Fi enquanto houver energia. NTP pode corrigir um ajuste manual.
O fuso e fixo em minutos e nao aplica horario de verao automaticamente.
O intervalo aceito para ajuste e de 2024 a 2037, compativel com o runtime ESP32.

## Armazenamento e acesso

SPIFFS armazena somente a pagina. Configuracoes e senha OTA LAN usam Preferences/NVS,
portanto um `uploadfs` normal nao apaga as agendas. Apagar toda a flash ou mudar
a tabela de particoes pode apagar tanto configuracoes quanto credenciais.
Falha na leitura da agenda bloqueia o modo automatico e aparece no painel/serial;
uma configuracao valida salva pela pagina libera novamente a operacao.

O painel, diagnostico, APIs e OTA Web usam o usuario `admin` e a senha
definida na compilacao. Para troca-la, atualize a configuracao local,
recompile e grave o firmware. Uma senha Web antiga na NVS e ignorada;
agendas e senha OTA LAN nao sao alteradas. Se o navegador insistir no login
antigo, use uma janela privativa.

O painel usa HTTP e autenticacao Basic, sem criptografia TLS. O portal de
configuracao e protegido pela senha da rede AP, nao pelo login HTTP do painel.
Use somente em rede local confiavel. Nao encaminhe portas do roteador,
nao publique na Internet e nao inclua senhas em repositorios.

## Relogio em tempo real

O controle principal de tempo segue o projeto modelo:

- Biblioteca `arduino-libraries/NTPClient` versao `3.2.1`, com `WiFiUDP`.
- Servidor `br.pool.ntp.org`.
- Fuso inicial Brasilia: `-3 * 3600` segundos.
- Intervalo NTP de `60000` ms (60 segundos).
- Chamada `ntp.update()` e leitura `ntp.getFormattedTime()` a cada segundo.
- Data e agendas derivadas de `ntp.getEpochTime()`, usando `gmtime_r` porque
  o NTPClient ja inclui o deslocamento do fuso no epoch retornado.

Nao utiliza mais `configTime`/SNTP como fonte principal. O fuso editavel ja
existente e preservado e aplicado com `ntp.setTimeOffset`; deixando `-180`,
o comportamento de fuso e o mesmo do modelo. O endpoint de status remove esse
deslocamento ao transmitir o timestamp UTC, evitando aplicar UTC-3 duas vezes.
O ajuste pelo navegador permanece como alternativa: usa o relogio interno
temporariamente e a proxima sincronizacao NTP bem-sucedida reassume o controle.
Sem NTP nem ajuste valido, o automatico continua desligado, em vez de tratar
um horario ainda nao sincronizado como real.

LCD e pagina mostram data e hora local com segundos. O LCD atualiza uma vez por
segundo; a pagina conta os segundos continuamente a partir do timestamp UTC do
ESP32, usando tempo monotonicamente decorrido no navegador, e ressincroniza com
o dispositivo a cada consulta de status (3 segundos). Nao usa a hora do computador
para exibir o relogio, exceto quando voce solicita explicitamente o ajuste.
Sem resposta do ESP32, a pagina indica falha de conexao; depois de 15 segundos
sem referencia atualizada, nao continua apresentando o horario como confirmado.
O fuso salvo no ESP32 e aplicado nas duas telas.

Este e o relogio interno do ESP32 sincronizado por NTP, nao um RTC externo com
bateria. Apos falta de energia, aguarda NTP ou ajuste pelo navegador.
Durante OTA, o LCD prioriza progresso/erros e bloqueio dos reles.

## Nova referencia v1.6.9 md4

O codigo de `Alarme_ESP32_v1.6.9_24102026_estavel_md4` foi consultado sem
modificar seus arquivos. Foram adotados os padroes uteis ao controlador:

| Area | Adaptacao |
| --- | --- |
| Hardware | ESC3E05 com ESP32-C3 Super Mini: CH1..CH4 em GPIO5/6/7/10; polaridade a confirmar. |
| Wi-Fi | WiFiManager nao bloqueante, portal 120 s, AP e reset iguais ao modelo. |
| Relogio | NTPClient, br.pool.ntp.org, UTC-3 padrao, sincronizacao 60 s. |
| LCD | Busca 0x23/0x27/0x3F no boot, SDA em D0 e SCL em D1, tamanho 20x4 configurado, backlight por evento e mensagem temporaria de 3 s. |
| Diagnostico | `/diag`, `/api/diag` e `/api/events`, com autenticacao do painel. |
| Eventos | Buffer circular de 40 entradas em RAM, hora local, uptime e mensagem. |
| OTA | Web e ArduinoOTA, preservando bloqueio dos quatro canais ao atualizar. |

Em `/diag` aparecem IP, SSID, RSSI, uptime, motivo de reset (codigo
`esp_reset_reason_t` do ESP32), memoria livre/minima, relogio, estado do LCD,
NVS, SPIFFS, OTA e os quatro canais. Nao expoe senhas.
O historico registra boot, transicoes de Wi-Fi, sincronizacao/ajuste da hora,
mudancas dos reles e modos, gravacao de configuracao e eventos/erros OTA.
As APIs enviam eventos do mais antigo ao mais novo; a pagina mostra o mais
recente primeiro. Ao atingir 40 entradas, substitui a mais antiga.
Nao grava eventos na flash e o historico e apagado ao reiniciar.
A pagina de diagnostico e embutida no firmware, funcionando sem SPIFFS.

O LCD mostra temporariamente alteracoes de modo/saida, confirmacao de
configuracao, ajuste de hora e reconexao de Wi-Fi. Depois de 3 s, volta ao
resumo dos quatro reles. Erros de NVS/agenda e a atualizacao OTA tem prioridade;
data e relogio continuam visiveis durante mensagens normais.

As funcoes especificas de alarme do modelo (sensores, buzina, latch e pulsos)
nao foram copiadas. Os quatro canais continuam independentes, com os mesmos
nomes, modos e agendas anteriores. A NVS e a senha OTA LAN sao preservadas.
Nao importa configuracoes do alarme.
A interface continua dedicada a iluminacao, nao foi substituida pelo painel
de setores. A seguranca de desligamento durante OTA permanece especifica
deste controlador.

## Atualizacao OTA (Web e LAN)

Segue os dois modos do projeto modelo: pagina `/update` e ArduinoOTA na rede.
O upload Web usa o mesmo componente `Update` do framework, com tratamento proprio
para bloquear os quatro reles, exibir progresso/erros e retornar HTTP de falha
quando a imagem e rejeitada. A pagina de recuperacao esta embutida no firmware
em [include/ota_page.h](include/ota_page.h), independente do SPIFFS.

### Pelo navegador

1. A primeira gravacao deste firmware deve ser feita por USB.
2. Na mesma rede do ESP32, clique em **Abrir atualizacao OTA** no painel, ou abra
   `http://IP_DO_ESP32/update`.
3. Entre com o usuario `admin` e a senha do painel.
4. Compile com `pio run` e selecione
   `.pio\build\esp32-c3-super-mini\firmware.bin` no campo **Firmware**.
5. Para atualizar o painel, execute `pio run --target buildfs` e envie
   `.pio\build\esp32-c3-super-mini\spiffs.bin` no campo **Pagina web / SPIFFS**.
6. Confirme o desligamento dos reles. Aguarde a confirmacao de gravacao e o
   reinicio. 100% do envio do navegador ainda nao confirma sucesso da flash.
7. Aguarde cerca de 15 segundos e volte ao painel. Se ambos os arquivos foram
   alterados, envie cada imagem separadamente, aguardando reiniciar entre elas.

Use somente imagens `.bin` deste projeto, do mesmo ESP32 e mesmo layout de
particoes. Nao envie `bootloader.bin`, `partitions.bin`, firmware combinado ou
imagens compactadas. A imagem SPIFFS deve ter exatamente o tamanho da particao.
O firmware e validado pelo componente Update do ESP32; a imagem SPIFFS nao tem
assinatura de aplicacao, portanto selecionar o arquivo correto e essencial.

### ArduinoOTA / PlatformIO pela rede

- Host: `arvore-natal.local` (ou use o IP do painel).
- Porta OTA: `3232`.
- Senha LAN: aleatoria, separada do painel e persistida na NVS, exibida no monitor
  serial durante cada boot como `OTA LAN ... / senha`.
- Atualizacao de firmware e SPIFFS suportadas; sem acesso pela Internet.

Para PlatformIO, acrescente temporariamente ao ambiente, sem versionar a senha:

```ini
upload_protocol = espota
upload_port = 192.168.1.100
upload_flags =
  --auth=SENHA_OTA_LAN_DO_SERIAL
```

Troque o IP e senha pelos do seu dispositivo. Use `pio run --target upload`
para firmware ou `pio run --target uploadfs` para SPIFFS. Para voltar a USB,
remova essas opcoes. O protocolo LAN exige que o firewall permita a comunicacao
do ESP32 com o computador; o endereco IP funciona mesmo sem descoberta mDNS.

### Comportamento durante OTA

- Uma atualizacao autenticada desliga os quatro canais e suspende agendas e
  comandos manuais. O LCD acende e mostra tipo de imagem e porcentagem.
- Durante OTA Web, a porcentagem no LCD e relativa a capacidade da particao;
  a barra do navegador mostra o envio HTTP. No LAN, usa o tamanho informado.
- O SPIFFS e desmontado antes de gravar. O painel pode ficar indisponivel nesse
  periodo; `/update` continua disponivel mesmo se o sistema de arquivos falhar.
- A senha e conferida antes de aceitar bytes Web; o OTA LAN tem autenticacao
  propria. Os dois transportes nao gravam simultaneamente.
- Erros, imagem invalida, interrupcao ou timeout sao reportados no LCD/serial
  e, quando a conexao permite, na pagina. Nao ha reinicio automatico por falha.
- Depois que uma tentativa comeca, os reles permanecem bloqueados ate reiniciar,
  mesmo se falhar. Tente enviar novamente ou use **Reiniciar ESP32 e retomar
  agendas** em `/update`; tambem pode reiniciar fisicamente.
- Sucesso reinicia automaticamente. Todos os canais voltam a Automatico apos
  o boot, aguardando hora valida antes de aplicar as agendas.
- NVS nao e apagada: agendas, nomes, fuso e senhas permanecem salvos.
- Nao corte energia e nao troque particoes por OTA. Uma atualizacao SPIFFS
  interrompida pode exigir novo envio em `/update` ou recuperacao por USB.

HTTP Basic e ArduinoOTA nao substituem TLS nem firmware assinado. Use somente
rede local confiavel e mantenha USB disponivel para recuperacao.

## Testes de software

O teste nativo compila e executa o mesmo [include/schedule.h](include/schedule.h)
usado pelo firmware, verificando limites de horario, todos os minutos da semana,
meia-noite, virada de mes/ano, fevereiro bissexto, sobreposicao e independencia.
Requer Python 3 e `g++` ou `clang++`:

```powershell
python .\tests\run_schedule_tests.py
# Ou indique um compilador:
python .\tests\run_schedule_tests.py --compiler "C:\caminho\g++.exe"
```

Se nao houver compilador nativo, o PlatformIO pode fornece-lo no Windows:

```powershell
pio pkg exec --package platformio/toolchain-gccmingw32 -- g++ --version
```

Compilar e testar em software nao comprova o acionamento eletrico. Antes do uso:

1. Sem cargas, acione Ligar/Desligar nos quatro canais e confira CH1 a CH4.
2. Crie um periodo proximo da hora atual e observe ligar/desligar em Automatico.
3. Teste uma janela noturna e a volta de Manual para Automatico.
4. Reinicie, confirme persistencia das agendas e bloqueio sem hora valida.
5. Teste sem Internet usando a sincronizacao pelo navegador.
6. So depois conecte as cargas com a instalacao eletrica conferida.

Para validar OTA na placa, mantenha inicialmente as cargas desconectadas:

1. Sem autenticacao, confira que `/update` solicita login e nao inicia gravacao.
2. Acione um rele manualmente e envie firmware valido por OTA Web; confira
   desligamento dos quatro canais, progresso no LCD, reinicio e agendas salvas.
3. Envie `spiffs.bin` no campo correspondente e confira a pagina apos reiniciar.
4. Teste arquivo invalido e interrupcao do envio; confira erro e permanencia dos
   reles desligados. Use `/update` para tentar novamente ou reiniciar.
5. Teste ArduinoOTA com senha incorreta e correta, tanto firmware como SPIFFS.
6. Confira data/segundos no LCD e na pagina, incluindo sincronizacao, mudanca
   de fuso, perda de Wi-Fi e retorno apos desligar a alimentacao.

Validacoes em software realizadas: build de firmware e SPIFFS; 25.952
verificacoes nativas da agenda; testes no navegador com API/transporte simulados
para relogio, virada da data, referencia expirada, bloqueio OTA, envio de arquivo,
progresso, erro HTTP, autenticacao recusada e conexao interrompida. Esses testes
nao substituem a gravacao OTA e a verificacao eletrica no ESP32 real.
