#pragma once

const char OTA_PAGE[] PROGMEM = R"OTA(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Atualizacao OTA - Arvore de Natal</title>
<style>
body{font:16px "Segoe UI",sans-serif;background:#f3f7f4;color:#162e28;margin:0;padding:20px}
main{max-width:720px;margin:auto;background:white;padding:24px;border:1px solid #d6e3dc;border-radius:14px}
h1{font-size:1.5rem}section{border-top:1px solid #d6e3dc;margin-top:20px;padding-top:12px}
label{display:block;margin:12px 0}input,button{font:inherit;max-width:100%;margin:8px 0}
button,a{color:#146347}button{padding:10px;cursor:pointer}
.warning{background:#fff0db;padding:12px;border-radius:8px;line-height:1.5}
#status{white-space:pre-wrap;overflow-wrap:anywhere}progress{width:100%;height:24px}
</style>
</head>
<body><main>
<h1>Atualizacao OTA</h1>
<p><a href="/">Voltar ao controle dos reles</a></p>
<p class="warning">Use somente arquivos deste projeto para ESP32-C3 Super Mini.
Ao iniciar o envio, os quatro reles serao desligados e bloqueados ate reiniciar.
Nao desligue a alimentacao. Em caso de falha, tente novamente ou reinicie
conscientemente para retomar as agendas. Atualize apenas na rede local confiavel.</p>
<section><h2>Firmware</h2>
<form action="/update" method="post" enctype="multipart/form-data">
<label>Arquivo firmware.bin<input type="file" name="firmware" accept=".bin" required></label>
<button type="submit">Enviar firmware</button></form></section>
<section><h2>Pagina web / SPIFFS</h2>
<p>Gere o arquivo com <code>pio run --target buildfs</code>.
O envio substitui a pagina inteira. Agendas e senhas na NVS sao preservadas.</p>
<form action="/update" method="post" enctype="multipart/form-data">
<label>Arquivo spiffs.bin<input type="file" name="filesystem" accept=".bin" required></label>
<button type="submit">Enviar SPIFFS</button></form></section>
<section><h2>Andamento</h2>
<progress id="progress" max="100" value="0" aria-label="Progresso do envio"></progress>
<p id="status" role="status" aria-live="polite">Aguardando arquivo.</p>
<p>100% do envio nao significa gravacao concluida: aguarde a confirmacao do ESP32.</p>
</section>
<section><h2>Recuperacao</h2>
<p>Esta pagina fica no firmware e funciona mesmo se o SPIFFS estiver danificado.
Se a atualizacao falhar, os reles permanecem desligados ate reiniciar.</p>
<form action="/api/restart" method="post" id="restart-form">
<button type="submit">Reiniciar ESP32 e retomar agendas</button></form></section>
<script>
'use strict';
let busy = false;
const statusBox = document.getElementById('status');
function controls(disabled) {
  document.querySelectorAll('button,input').forEach(element => { element.disabled = disabled; });
}
document.querySelectorAll('form[enctype]').forEach(form => {
  form.addEventListener('submit', event => {
    event.preventDefault();
    if (busy || !form.reportValidity()) return;
    const input = form.querySelector('input[type=file]');
    const file = input.files[0];
    if (!file || !file.size || !file.name.toLowerCase().endsWith('.bin')) {
      statusBox.textContent = 'Selecione um arquivo .bin nao vazio.';
      return;
    }
    if (!confirm('Desligar os quatro reles e iniciar a atualizacao?')) return;
    const data = new FormData(form);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update');
    xhr.timeout = 180000;
    busy = true;
    controls(true);
    document.getElementById('progress').value = 0;
    statusBox.textContent = 'Enviando. Aguarde a confirmacao de gravacao.';
    xhr.upload.onprogress = event => {
      if (event.lengthComputable) document.getElementById('progress').value = Math.floor(event.loaded * 100 / event.total);
    };
    xhr.onload = () => {
      const success = xhr.status === 200;
      statusBox.textContent = xhr.status === 401 ? 'Login necessario. Recarregue e entre com admin e a senha do painel.' :
        xhr.responseText || `Falha HTTP ${xhr.status}. Confira o monitor serial.`;
      if (success) statusBox.textContent += ' Aguarde cerca de 15 segundos e volte ao painel.';
      busy = success;
      controls(success);
    };
    const failed = () => {
      busy = false;
      controls(false);
      statusBox.textContent = 'Conexao interrompida ou tempo esgotado. Nao ha confirmacao de sucesso. Confira o LCD/serial e tente novamente; os reles podem estar bloqueados ate reiniciar.';
    };
    xhr.onerror = failed;
    xhr.ontimeout = failed;
    xhr.onabort = failed;
    xhr.send(data);
  });
});
document.getElementById('restart-form').addEventListener('submit', event => {
  if (busy || !confirm('Reiniciar? Depois da sincronizacao, os reles voltarao a seguir as agendas.')) event.preventDefault();
});
</script>
</main></body></html>)OTA";
