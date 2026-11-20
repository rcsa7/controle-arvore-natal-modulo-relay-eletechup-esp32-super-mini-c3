#pragma once

const char DIAG_PAGE[] PROGMEM = R"DIAG(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Diagnostico - Arvore de Natal</title>
<style>
body{font:16px "Segoe UI",sans-serif;background:#f3f7f4;color:#162e28;padding:16px}
main{max-width:1000px;margin:auto}a{color:#146347}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:white;padding:16px;border:1px solid #d6e3dc;border-radius:10px}
table{border-collapse:collapse;width:100%;background:white}td,th{text-align:left;padding:10px;border:1px solid #d6e3dc;overflow-wrap:anywhere}
.scroll{overflow:auto}#message{padding:12px;background:#fff0db}button{padding:10px;font:inherit}
</style></head><body><main>
<h1>Diagnostico e eventos</h1>
<p><a href="/">Painel</a> | <a href="/update">Atualizacao OTA</a></p>
<p>Referencia: Alarme ESP32 v1.6.9 md4. Os ultimos 40 eventos ficam na RAM e sao perdidos ao reiniciar.</p>
<button id="refresh">Atualizar agora</button>
<p id="message" role="status" aria-live="polite">Consultando ESP32...</p>
<h2>Estado do equipamento</h2><pre id="diag">Aguardando...</pre>
<h2>Eventos recentes (mais novos primeiro)</h2>
<div class="scroll"><table><thead><tr><th>Hora local</th><th>Uptime (ms)</th><th>Evento</th></tr></thead><tbody id="events"></tbody></table></div>
<script>
'use strict';
let busy = false;
async function getJson(path) {
  const response = await fetch(path, {cache:'no-store', signal:AbortSignal.timeout(10000)});
  if (!response.ok) throw new Error(response.status === 401 ?
    'Login necessario. Recarregue e entre com admin e a senha do painel.' : `HTTP ${response.status}`);
  return response.json();
}
async function refresh() {
  if (busy) return;
  busy = true;
  document.getElementById('refresh').disabled = true;
  try {
    const [diag, log] = await Promise.all([getJson('/api/diag'), getJson('/api/events')]);
    document.getElementById('diag').textContent = JSON.stringify(diag, null, 2);
    const rows = log.events.slice().reverse().map(entry => {
      const row = document.createElement('tr');
      [entry.time, entry.uptimeMs, entry.message].forEach(value => {
        const cell = document.createElement('td');
        cell.textContent = String(value);
        row.append(cell);
      });
      return row;
    });
    document.getElementById('events').replaceChildren(...rows);
    document.getElementById('message').textContent = `Atualizado. ${log.count} de ${log.capacity} eventos.`;
  } catch (error) {
    document.getElementById('message').textContent = `Falha: ${error.message} Dados anteriores podem estar desatualizados.`;
  } finally {
    busy = false;
    document.getElementById('refresh').disabled = false;
  }
}
document.getElementById('refresh').addEventListener('click', refresh);
refresh();
setInterval(refresh, 5000);
</script></main></body></html>)DIAG";
