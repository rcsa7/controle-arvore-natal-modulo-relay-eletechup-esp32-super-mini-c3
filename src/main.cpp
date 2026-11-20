/*
 * Pinagem: ESP32-C3 Super Mini na placa ESC3E05
 *
 * Reles (ligacoes internas, definidas em include/schedule.h):
 *   CH1 = GPIO5, CH2 = GPIO6, CH3 = GPIO7, CH4 = GPIO10.
 *   O firmware usa HIGH para ligar; confirme a polaridade sem cargas.
 *
 * LCD 2004 I2C (sinais de 3,3 V):
 *   SDA = GPIO0 (D0), SCL = GPIO1 (D1).
 *
 * Botoes normalmente abertos para GND, com pull-up interno:
 *   ACK / consulta do LCD = GPIO3 (D3).
 *   Reset do Wi-Fi = GPIO4 (D4); pressione por 8 segundos.
 *
 * Deixe os bornes D2 e D8 livres para o boot. Sem controle do LED integrado.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "ota_page.h"
#include "diag_page.h"
#include "schedule.h"
#include "lcd_detection.h"
#if !__has_include("local_credentials.h")
#error "Copie include/local_credentials.example.h para include/local_credentials.h e configure as senhas."
#else
#include "local_credentials.h"
#endif

using namespace natal;

constexpr char HOSTNAME[] = "arvore-natal_supermini";
constexpr char AP_NAME[] = "Arvore_Natal_ESP32_supermini";
static_assert(sizeof(PANEL_PASSWORD) >= 9, "PANEL_PASSWORD deve ter pelo menos 8 bytes.");
static_assert(sizeof(AP_PASSWORD) >= 9 && sizeof(AP_PASSWORD) <= 64,
              "AP_PASSWORD deve ter de 8 a 63 bytes.");
constexpr uint8_t ACK_BUTTON_PIN = 3;
constexpr uint8_t WIFI_RESET_BUTTON_PIN = 4;
constexpr int I2C_SDA_PIN = 0;
constexpr int I2C_SCL_PIN = 1;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 120000;
constexpr uint32_t LCD_BACKLIGHT_HOLD_MS = 8000;
constexpr long BRAZIL_UTC_OFFSET_SECONDS = -3 * 3600;
constexpr time_t MIN_TIME = 1704067200;  // 2024-01-01 UTC
constexpr time_t MAX_TIME = 2145916800;  // 2038-01-01; compativel com time_t de 32 bits
constexpr size_t JSON_CAPACITY = 32768;
WebServer server(80);
WiFiManager wifiManager;
Preferences storage;
WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP, "br.pool.ntp.org", BRAZIL_UTC_OFFSET_SECONDS, 60000);
bool manualClock = false;
String currentTimeFormatted = "--:--:--";
uint32_t lastClockCheck = 0;
uint32_t lastNtpWarning = 0;
RelayConfig relays[RELAY_COUNT];
enum class Mode : uint8_t { Automatic, On, Off };
Mode modes[RELAY_COUNT] = {};
bool outputs[RELAY_COUNT] = {};
bool storageReady = false;
bool configHealthy = true;
bool filesystemReady = false;
bool webReady = false;
bool wifiConfigPortalActive = false;
bool lcdReady = false;
bool lcdLightOn = false;
LiquidCrystal_I2C lcd(0x27, LCD_COLUMNS, LCD_ROWS);
uint8_t lcdAddress = 0;
String lcdDiagnostic = "aguardando";
String lcdLines[4];
uint32_t lcdBacklightUntil = 0;
uint32_t lastLcdUpdate = 0;
int utcOffsetMinutes = -180;
String otaLanPassword;
bool otaActive = false;
bool otaLocked = false;
bool otaWebTransfer = false;
bool webUploadSeen = false;
bool webUploadComplete = false;
bool webUploadAuthorized = false;
bool otaFilesystem = false;
uint8_t otaPercent = 0;
String otaError;
uint32_t otaRestartAt = 0;
uint32_t otaLastActivity = 0;
String clockSource = "aguardando";
uint32_t lastEvaluation = 0;
uint32_t resetStarted = 0;
constexpr char REFERENCE_MODEL[] = "Alarme_ESP32_v1.6.9_24102026_estavel_md4";
constexpr uint8_t EVENT_LOG_CAPACITY = 40;
struct EventLogEntry {
    uint32_t uptimeMs;
    char timeText[10];
    char message[96];
};
EventLogEntry eventLog[EVENT_LOG_CAPACITY];
uint8_t eventLogHead = 0;
uint8_t eventLogCount = 0;
String lcdTempLine3;
uint32_t lcdTempUntil = 0;
bool previousWifiConnected = false;

void addEventLog(const String& message) {
    EventLogEntry& entry = eventLog[eventLogHead];
    entry.uptimeMs = millis();
    strlcpy(entry.timeText, currentTimeFormatted.c_str(), sizeof(entry.timeText));
    strlcpy(entry.message, message.c_str(), sizeof(entry.message));
    eventLogHead = (eventLogHead + 1) % EVENT_LOG_CAPACITY;
    if (eventLogCount < EVENT_LOG_CAPACITY) ++eventLogCount;
    Serial.printf("[EVENT] %s %s\n", entry.timeText, entry.message);
}

void requestLcdBacklight(uint32_t duration = LCD_BACKLIGHT_HOLD_MS) {
    if (!lcdReady) return;
    const uint32_t until = millis() + duration;
    if (static_cast<int32_t>(until - lcdBacklightUntil) > 0) lcdBacklightUntil = until;
    if (!lcdLightOn) {
        lcd.backlight();
        lcdLightOn = true;
    }
}

void lcdPrintLine(uint8_t row, const String& text) {
    if (!lcdReady) return;
    String line = text.substring(0, 20);
    while (line.length() < 20) line += ' ';
    if (lcdLines[row] == line) return;
    lcd.setCursor(0, row);
    lcd.print(line);
    lcdLines[row] = line;
}

void showTemporaryLcdLine3(const String& text) {
    lcdTempLine3 = text.substring(0, 20);
    lcdTempUntil = millis() + 3000;
    requestLcdBacklight();
    if (!otaLocked) lcdPrintLine(3, lcdTempLine3);
}

void applyLcdBacklightPolicy() {
    if (!lcdReady) return;
    const bool on = wifiConfigPortalActive || otaLocked ||
        static_cast<int32_t>(millis() - lcdBacklightUntil) < 0;
    if (on == lcdLightOn) return;
    if (on) lcd.backlight();
    else lcd.noBacklight();
    lcdLightOn = on;
}

void checkWifiReset() {
    if (otaActive || otaRestartAt) return;
    if (digitalRead(WIFI_RESET_BUTTON_PIN) != LOW) {
        resetStarted = 0;
        return;
    }
    if (!resetStarted) resetStarted = millis();
    if (millis() - resetStarted < 8000) return;
    for (uint8_t i = 0; i < RELAY_COUNT; ++i)
        digitalWrite(RELAY_PINS[i], !RELAY_ON_LEVEL);
    addEventLog("Reset de Wi-Fi solicitado. Agendas e senha preservadas.");
    lcdPrintLine(3, "Reset Wi-Fi...");
    requestLcdBacklight();
    wifiManager.resetSettings();
    ESP.restart();
}

void output(uint8_t index, bool on) {
    if (otaLocked) on = false;
    if (outputs[index] != on) {
        const String event = "CH" + String(index + 1) + (on ? " LIGADO" : " DESLIGADO");
        addEventLog(event);
        showTemporaryLcdLine3(event);
        requestLcdBacklight();
    }
    outputs[index] = on;
    digitalWrite(RELAY_PINS[index], on ? RELAY_ON_LEVEL : !RELAY_ON_LEVEL);
}

void otaLcd() {
    requestLcdBacklight();
    lcdPrintLine(0, "Atualizacao OTA");
    lcdPrintLine(1, otaFilesystem ? "SPIFFS / pagina web" : "Firmware ESP32");
    lcdPrintLine(2, otaError.length() ? "ERRO: veja web/serial" :
                 otaRestartAt ? "Concluido: reinicio" : "Progresso: " + String(otaPercent) + "%");
    lcdPrintLine(3, "4 reles BLOQUEADOS");
}

void beginOta(bool filesystem, bool web) {
    otaLocked = true;
    otaActive = true;
    otaWebTransfer = web;
    otaFilesystem = filesystem;
    otaPercent = 0;
    otaError = "";
    otaLastActivity = millis();
    for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
        modes[i] = Mode::Off;
        output(i, false);
    }
    if (filesystem) {
        SPIFFS.end();
        filesystemReady = false;
    }
    Serial.printf("OTA %s: iniciando %s; 4 reles bloqueados.\n",
                  web ? "Web" : "LAN", filesystem ? "SPIFFS" : "firmware");
    addEventLog(String("OTA ") + (web ? "Web " : "LAN ") +
                (filesystem ? "SPIFFS" : "firmware") + ": reles bloqueados");
    otaLcd();
}

void failOta(const String& error) {
    otaError = error;
    if (Update.isRunning()) Update.abort();
    otaActive = false;
    otaWebTransfer = false;
    Serial.printf("ERRO OTA: %s. Reles bloqueados ate reiniciar.\n", error.c_str());
    addEventLog("ERRO OTA: " + error);
    otaLcd();
}

void otaProgress(size_t progress, size_t total) {
    otaLastActivity = millis();
    const uint8_t percent = total ? static_cast<uint8_t>((uint64_t(progress) * 100) / total) : 0;
    if (percent == otaPercent) return;
    otaPercent = percent;
    Serial.printf("OTA: %u%%\n", otaPercent);
    otaLcd();
}

void completeOta() {
    otaActive = false;
    otaWebTransfer = false;
    otaPercent = 100;
    otaRestartAt = millis() + 1500;
    addEventLog("OTA concluido. Reiniciando com reles desligados.");
    otaLcd();
}

time_t currentUtc() {
    if (!manualClock && ntp.isTimeSet()) {
        // NTPClient ja inclui o fuso; a API e o navegador recebem UTC.
        return static_cast<time_t>(static_cast<int64_t>(ntp.getEpochTime()) -
                                   utcOffsetMinutes * 60);
    }
    return time(nullptr);
}

void updateClock() {
    if (ntp.update()) {
        const bool firstSync = clockSource != "NTP";
        manualClock = false;
        clockSource = "NTP";
        if (firstSync) {
            currentTimeFormatted = ntp.getFormattedTime();
            addEventLog("Relogio sincronizado: br.pool.ntp.org (NTPClient).");
        }
    } else if (WiFi.status() != WL_CONNECTED || !ntp.isTimeSet()) {
        if (!lastNtpWarning || millis() - lastNtpWarning >= 60000) {
            lastNtpWarning = millis();
            Serial.println("AVISO: NTP sem sincronizacao/rede; mantendo hora existente, se valida.");
        }
    }
    if (!manualClock && ntp.isTimeSet()) {
        currentTimeFormatted = ntp.getFormattedTime();
    } else {
        const time_t local = currentUtc() + utcOffsetMinutes * 60;
        tm parts{};
        char text[9];
        if (currentUtc() >= MIN_TIME && currentUtc() < MAX_TIME) {
            gmtime_r(&local, &parts);
            strftime(text, sizeof(text), "%H:%M:%S", &parts);
            currentTimeFormatted = text;
        } else {
            currentTimeFormatted = "--:--:--";
        }
    }
}

bool localMinute(LocalMinute& result, String* formatted = nullptr) {
    const time_t utc = currentUtc();
    if (utc < MIN_TIME || utc >= MAX_TIME) return false;
    const time_t local = utc + utcOffsetMinutes * 60;
    const time_t previous = local - 86400;
    tm today{}, yesterday{};
    gmtime_r(&local, &today);
    gmtime_r(&previous, &yesterday);
    result = {static_cast<uint8_t>(today.tm_wday), static_cast<uint8_t>(today.tm_mday),
              static_cast<uint8_t>(yesterday.tm_mday),
              static_cast<uint16_t>(today.tm_hour * 60 + today.tm_min)};
    if (formatted) {
        char text[24];
        strftime(text, sizeof(text), "%d/%m/%Y %H:%M:%S", &today);
        *formatted = text;
    }
    return true;
}

void updateLcdStatus() {
    applyLcdBacklightPolicy();
    if (otaLocked) {
        otaLcd();
        return;
    }
    lcdPrintLine(1, WiFi.status() == WL_CONNECTED ?
                 "IP: " + WiFi.localIP().toString() : "Wi-Fi desconectado");
    LocalMinute now{};
    String formatted;
    const bool clockValid = localMinute(now, &formatted);
    lcdPrintLine(0, clockValid ? "Natal " + formatted.substring(0, 10) : "Arvore Natal v1.0.0");
    lcdPrintLine(2, clockValid ? "Hora: " + currentTimeFormatted : "Hora: sincronizar");
    String states;
    for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
        if (i) states += ' ';
        states += String(i + 1) + (outputs[i] ? ":L" : ":D") +
            (modes[i] == Mode::Automatic ? "A" : "M");
    }
    const bool temporary = lcdTempLine3.length() &&
        static_cast<int32_t>(millis() - lcdTempUntil) < 0;
    lcdPrintLine(3, !storageReady ? "ERRO memoria NVS" :
                 !configHealthy ? "ERRO agenda salva" : temporary ? lcdTempLine3 : states);
}

void evaluate() {
    LocalMinute now{};
    const bool valid = localMinute(now);
    for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
        const bool on = modes[i] == Mode::On ||
            (modes[i] == Mode::Automatic && configHealthy && valid && scheduledOn(relays[i], now));
        output(i, on);
    }
}

void checkAckButton() {
    static int lastReading = HIGH;
    static int stableState = HIGH;
    static uint32_t changedAt = 0;
    const int reading = digitalRead(ACK_BUTTON_PIN);
    if (reading != lastReading) {
        lastReading = reading;
        changedAt = millis();
    }
    if (millis() - changedAt < 50 || reading == stableState) return;
    stableState = reading;
    if (stableState != LOW) return;
    requestLcdBacklight();
    addEventLog("ACK: consulta LCD, sem alterar reles.");
    if (!wifiConfigPortalActive) {
        lcdTempLine3 = "";
        updateLcdStatus();
    }
}

void minuteText(uint16_t minute, char* buffer) {
    snprintf(buffer, 6, "%02u:%02u", minute / 60, minute % 60);
}

void writeRelay(JsonObject object, const RelayConfig& config) {
    object["name"] = config.name;
    JsonArray rules = object.createNestedArray("rules");
    for (uint8_t i = 0; i < config.count; ++i) {
        const Rule& rule = config.rules[i];
        JsonObject item = rules.createNestedObject();
        item["enabled"] = rule.enabled;
        item["kind"] = rule.monthly ? "monthly" : "weekly";
        item["days"] = rule.days;
        item["monthDay"] = rule.monthDay;
        char text[6];
        minuteText(rule.on, text);
        item["on"] = String(text);
        minuteText(rule.off, text);
        item["off"] = String(text);
    }
}

bool readRelay(JsonObjectConst object, RelayConfig& config, String& error) {
    if (!object["name"].is<const char*>() || !object["rules"].is<JsonArrayConst>()) {
        error = "Nome e lista de periodos obrigatorios.";
        return false;
    }
    const char* name = object["name"];
    if (!strlen(name) || strlen(name) >= sizeof(config.name)) {
        error = "Nome deve ter entre 1 e 48 bytes.";
        return false;
    }
    for (const char* c = name; *c; ++c) {
        if (static_cast<unsigned char>(*c) < 32) {
            error = "Nome contem caractere de controle.";
            return false;
        }
    }
    const JsonArrayConst rules = object["rules"];
    if (rules.size() > MAX_RULES) {
        error = "Limite de 16 periodos por rele.";
        return false;
    }
    strlcpy(config.name, name, sizeof(config.name));
    config.count = 0;
    for (JsonObjectConst item : rules) {
        Rule rule;
        if (!item["enabled"].is<bool>() || !item["kind"].is<const char*>() ||
            !item["days"].is<int>() || !item["monthDay"].is<int>() ||
            !item["on"].is<const char*>() || !item["off"].is<const char*>()) {
            error = "Periodo com campos ausentes ou tipos invalidos.";
            return false;
        }
        const String kind = item["kind"].as<String>();
        const int days = item["days"];
        const int day = item["monthDay"];
        if ((kind != "weekly" && kind != "monthly") || days < 0 || days > 127 ||
            day < 1 || day > 31 || (kind == "weekly" && days == 0) ||
            !parseMinute(item["on"], rule.on) || !parseMinute(item["off"], rule.off) ||
            rule.on == rule.off) {
            error = "Confira dias e horarios. Ligar e desligar devem ser diferentes.";
            return false;
        }
        rule.enabled = item["enabled"];
        rule.monthly = kind == "monthly";
        rule.days = days;
        rule.monthDay = day;
        config.rules[config.count++] = rule;
    }
    return true;
}

bool readConfiguration(JsonDocument& doc, RelayConfig* configs, int& offset, String& error) {
    if (!doc["version"].is<int>() || doc["version"].as<int>() != 1 ||
        !doc["utcOffset"].is<int>() || !doc["relays"].is<JsonArray>() ||
        doc["relays"].size() != RELAY_COUNT) {
        error = "Formato de configuracao invalido.";
        return false;
    }
    offset = doc["utcOffset"];
    if (offset < -720 || offset > 840) {
        error = "Fuso deve estar entre UTC-12 e UTC+14.";
        return false;
    }
    for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
        if (!readRelay(doc["relays"][i].as<JsonObjectConst>(), configs[i], error)) return false;
    }
    return true;
}

void configuration(JsonDocument& doc) {
    doc["version"] = 1;
    doc["utcOffset"] = utcOffsetMinutes;
    JsonArray array = doc.createNestedArray("relays");
    for (const RelayConfig& relay : relays) writeRelay(array.createNestedObject(), relay);
}

bool persist(JsonDocument& doc) {
    if (!storageReady || doc.overflowed()) return false;
    String encoded;
    if (!serializeJson(doc, encoded)) return false;
    return storage.putString("config", encoded) == encoded.length();
}

void jsonError(int code, const String& error) {
    addEventLog("HTTP " + String(code) + ": " + error);
    StaticJsonDocument<256> doc;
    doc["error"] = error;
    String text;
    serializeJson(doc, text);
    server.send(code, "application/json", text);
}

bool authorized() {
    if (server.authenticate("admin", PANEL_PASSWORD)) return true;
    server.requestAuthentication();
    return false;
}

bool body(JsonDocument& doc, size_t limit) {
    if (otaLocked) {
        jsonError(423, "Reles e configuracoes bloqueados por OTA. Reinicie ou conclua a atualizacao em /update.");
        return false;
    }
    if (server.header("Content-Type") != "application/json") {
        jsonError(415, "Envie Content-Type application/json.");
        return false;
    }
    if (!server.hasArg("plain") || server.arg("plain").length() > limit) {
        jsonError(413, "Corpo ausente ou maior que o limite.");
        return false;
    }
    const DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        jsonError(400, "JSON invalido: " + String(error.c_str()));
        return false;
    }
    requestLcdBacklight();
    return true;
}

void status() {
    if (!authorized()) return;
    evaluate();
    DynamicJsonDocument doc(2048);
    LocalMinute now{};
    String text;
    doc["clockValid"] = localMinute(now, &text);
    doc["epoch"] = static_cast<int64_t>(currentUtc());
    doc["current_time"] = currentTimeFormatted;
    doc["time"] = text;
    doc["clockSource"] = clockSource;
    doc["utcOffset"] = utcOffsetMinutes;
    doc["configHealthy"] = configHealthy;
    doc["storageReady"] = storageReady;
    doc["filesystemReady"] = filesystemReady;
    doc["lcdReady"] = lcdReady;
    doc["lcdAddress"] = lcdReady ? String("0x") + String(lcdAddress, HEX) : String("");
    doc["lcdDiagnostic"] = lcdDiagnostic;
    doc["network"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Wi-Fi desconectado";
    doc["ip"] = WiFi.localIP().toString();
    doc["version"] = "1.0.0";
    doc["otaLocked"] = otaLocked;
    doc["otaActive"] = otaActive;
    doc["otaProgress"] = otaPercent;
    doc["otaError"] = otaError;
    JsonArray array = doc.createNestedArray("relays");
    for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
        JsonObject item = array.createNestedObject();
        item["id"] = i;
        item["name"] = relays[i].name;
        item["gpio"] = RELAY_PINS[i];
        item["on"] = outputs[i];
        item["mode"] = modes[i] == Mode::Automatic ? "auto" : modes[i] == Mode::On ? "on" : "off";
    }
    String encoded;
    serializeJson(doc, encoded);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", encoded);
}

void webOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        webUploadAuthorized = server.authenticate("admin", PANEL_PASSWORD);
        if (!webUploadAuthorized) return;
        if (otaRestartAt || (otaActive && !otaWebTransfer)) return;
        if (webUploadSeen) {
            failOta("Envie somente um arquivo por requisicao.");
            webUploadComplete = false;
            return;
        }
        webUploadSeen = true;
        webUploadComplete = false;
        const bool filesystem = upload.name == "filesystem";
        if ((upload.name != "firmware" && !filesystem) ||
            !upload.filename.endsWith(".bin")) {
            // Nenhuma gravacao foi iniciada para este arquivo.
            otaError = "Selecione firmware.bin ou spiffs.bin no campo correspondente.";
            Serial.println("ERRO OTA Web: " + otaError);
            return;
        }
        beginOta(filesystem, true);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, filesystem ? U_SPIFFS : U_FLASH)) {
            failOta("Falha ao iniciar gravacao: " + String(Update.errorString()));
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!webUploadAuthorized || !otaActive || !otaWebTransfer || otaError.length()) return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            failOta("Falha ao gravar: " + String(Update.errorString()));
            return;
        }
        otaProgress(Update.progress(), Update.size());
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!webUploadAuthorized || !otaActive || !otaWebTransfer || otaError.length()) return;
        if (upload.totalSize == 0 || (otaFilesystem && upload.totalSize != Update.size())) {
            failOta("Arquivo vazio ou tamanho incorreto da imagem SPIFFS.");
            return;
        }
        if (!Update.end(!otaFilesystem)) {
            failOta("Imagem rejeitada: " + String(Update.errorString()));
            return;
        }
        webUploadComplete = true;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (!webUploadAuthorized) return;
        if (otaActive && otaWebTransfer) failOta("Envio cancelado ou conexao interrompida.");
        webUploadSeen = false;
        webUploadComplete = false;
        webUploadAuthorized = false;
    }
}

void startOta() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(otaLanPassword.c_str());
    ArduinoOTA.setRebootOnSuccess(false);
    ArduinoOTA.onStart([] {
        beginOta(ArduinoOTA.getCommand() == U_SPIFFS, false);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        otaProgress(progress, total);
    });
    ArduinoOTA.onEnd(completeOta);
    ArduinoOTA.onError([](ota_error_t error) {
        const char* reason = error == OTA_AUTH_ERROR ? "Autenticacao LAN recusada" :
            error == OTA_BEGIN_ERROR ? "Falha ao iniciar OTA LAN" :
            error == OTA_CONNECT_ERROR ? "Falha de conexao LAN" :
            error == OTA_RECEIVE_ERROR ? "Falha ao receber imagem LAN" : "Falha ao finalizar imagem LAN";
        if (otaActive) failOta(reason);
        else Serial.printf("ERRO OTA: %s (%u)\n", reason, error);
    });
    ArduinoOTA.begin();
    Serial.printf("OTA LAN: %s.local:3232 / senha: %s\n", HOSTNAME, otaLanPassword.c_str());
}

void startWeb() {
    const char* headers[] = {"Content-Type"};
    server.collectHeaders(headers, 1);
    server.on("/diag", HTTP_GET, [] {
        if (!authorized()) return;
        server.sendHeader("Cache-Control", "no-store");
        server.send_P(200, "text/html; charset=utf-8", DIAG_PAGE);
    });
    server.on("/api/diag", HTTP_GET, [] {
        if (!authorized()) return;
        DynamicJsonDocument doc(3072);
        doc["version"] = "1.0.0";
        doc["referenceModel"] = REFERENCE_MODEL;
        doc["uptimeMs"] = millis();
        doc["resetReason"] = static_cast<int>(esp_reset_reason());
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["minFreeHeap"] = ESP.getMinFreeHeap();
        doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        if (WiFi.status() == WL_CONNECTED) doc["rssi"] = WiFi.RSSI();
        LocalMinute now{};
        String text;
        doc["clockValid"] = localMinute(now, &text);
        doc["time"] = text;
        doc["clockSource"] = clockSource;
        doc["utcOffset"] = utcOffsetMinutes;
        doc["lcdReady"] = lcdReady;
        doc["lcdAddress"] = lcdReady ? String("0x") + String(lcdAddress, HEX) : String("");
        doc["lcdDiagnostic"] = lcdDiagnostic;
        doc["lcdColumns"] = LCD_COLUMNS;
        doc["lcdRows"] = LCD_ROWS;
        doc["storageReady"] = storageReady;
        doc["filesystemReady"] = filesystemReady;
        doc["configHealthy"] = configHealthy;
        doc["otaLocked"] = otaLocked;
        doc["otaActive"] = otaActive;
        doc["otaError"] = otaError;
        JsonArray array = doc.createNestedArray("relays");
        for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
            JsonObject item = array.createNestedObject();
            item["channel"] = i + 1;
            item["gpio"] = RELAY_PINS[i];
            item["on"] = outputs[i];
            item["mode"] = modes[i] == Mode::Automatic ? "auto" : modes[i] == Mode::On ? "on" : "off";
            item["periods"] = relays[i].count;
        }
        String encoded;
        serializeJson(doc, encoded);
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "application/json", encoded);
    });
    server.on("/api/events", HTTP_GET, [] {
        if (!authorized()) return;
        DynamicJsonDocument doc(16384);
        doc["count"] = eventLogCount;
        doc["capacity"] = EVENT_LOG_CAPACITY;
        JsonArray events = doc.createNestedArray("events");
        const uint8_t start = (eventLogHead + EVENT_LOG_CAPACITY - eventLogCount) % EVENT_LOG_CAPACITY;
        for (uint8_t i = 0; i < eventLogCount; ++i) {
            const EventLogEntry& entry = eventLog[(start + i) % EVENT_LOG_CAPACITY];
            JsonObject item = events.createNestedObject();
            item["uptimeMs"] = entry.uptimeMs;
            item["time"] = entry.timeText;
            item["message"] = entry.message;
        }
        String encoded;
        serializeJson(doc, encoded);
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "application/json", encoded);
    });
    server.on("/update", HTTP_GET, [] {
        if (!authorized()) return;
        server.sendHeader("Cache-Control", "no-store");
        server.send_P(200, "text/html; charset=utf-8", OTA_PAGE);
    });
    server.on("/update", HTTP_POST, [] {
        if (!authorized()) return;
        if (otaRestartAt || (otaActive && !otaWebTransfer)) {
            server.send(409, "text/plain; charset=utf-8", "Outra atualizacao ou reinicio esta em andamento.");
            return;
        }
        const bool success = webUploadSeen && webUploadComplete && otaError.isEmpty();
        webUploadSeen = false;
        webUploadComplete = false;
        webUploadAuthorized = false;
        server.sendHeader("Cache-Control", "no-store");
        server.sendHeader("Connection", "close");
        if (!success) {
            if (otaActive && otaWebTransfer) failOta("Envio incompleto.");
            server.send(400, "text/plain; charset=utf-8",
                        "Falha OTA: " + (otaError.length() ? otaError : String("Nenhum arquivo recebido.")) +
                        (otaLocked ? " Reles bloqueados. Tente novamente ou reinicie." : ""));
            return;
        }
        server.send(200, "text/plain; charset=utf-8", "Atualizacao gravada com sucesso. ESP32 reiniciando.");
        completeOta();
    }, webOtaUpload);
    server.on("/api/restart", HTTP_POST, [] {
        if (!authorized()) return;
        if (otaActive) {
            jsonError(409, "Aguarde o termino da atualizacao antes de reiniciar.");
            return;
        }
        otaLocked = true;
        for (uint8_t i = 0; i < RELAY_COUNT; ++i) output(i, false);
        otaRestartAt = millis() + 1500;
        addEventLog("Reinicio solicitado pela pagina de atualizacao.");
        server.send(200, "text/plain; charset=utf-8", "ESP32 reiniciando. Aguarde 15 segundos e volte ao painel.");
    });
    server.on("/", HTTP_GET, [] {
        if (!authorized()) return;
        if (!filesystemReady) {
            server.send(503, "text/html; charset=utf-8",
                        "SPIFFS indisponivel. <a href=\"/update\">Atualizar firmware ou SPIFFS</a>.");
            return;
        }
        File file = SPIFFS.open("/index.html", FILE_READ);
        if (!file) {
            server.send(503, "text/html; charset=utf-8",
                        "Painel ausente. <a href=\"/update\">Enviar SPIFFS por OTA</a> ou execute uploadfs.");
            return;
        }
        server.sendHeader("Cache-Control", "no-store");
        server.streamFile(file, "text/html; charset=utf-8");
        file.close();
    });
    server.on("/api/status", HTTP_GET, status);
    server.on("/api/config", HTTP_GET, [] {
        if (!authorized()) return;
        DynamicJsonDocument doc(JSON_CAPACITY);
        configuration(doc);
        String encoded;
        serializeJson(doc, encoded);
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "application/json", encoded);
    });
    server.on("/api/config", HTTP_POST, [] {
        if (!authorized()) return;
        DynamicJsonDocument doc(JSON_CAPACITY);
        if (!body(doc, 20000)) return;
        RelayConfig candidate[RELAY_COUNT];
        int offset;
        String error;
        if (!readConfiguration(doc, candidate, offset, error)) {
            jsonError(400, error);
            return;
        }
        if (!persist(doc)) {
            jsonError(500, "Falha ao salvar na memoria NVS. Alteracoes nao aplicadas.");
            return;
        }
        for (uint8_t i = 0; i < RELAY_COUNT; ++i) relays[i] = candidate[i];
        utcOffsetMinutes = offset;
        ntp.setTimeOffset(utcOffsetMinutes * 60);
        configHealthy = true;
        evaluate();
        addEventLog("Configuracao dos reles e fuso salva na NVS.");
        showTemporaryLcdLine3("Configuracao salva");
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/mode", HTTP_POST, [] {
        if (!authorized()) return;
        StaticJsonDocument<256> doc;
        if (!body(doc, 128)) return;
        if (!doc["id"].is<int>() || !doc["mode"].is<const char*>()) {
            jsonError(400, "Informe id e modo.");
            return;
        }
        const int id = doc["id"];
        const String mode = doc["mode"].as<String>();
        if (id < 0 || id >= RELAY_COUNT || (mode != "auto" && mode != "on" && mode != "off")) {
            jsonError(400, "Rele ou modo invalido.");
            return;
        }
        modes[id] = mode == "auto" ? Mode::Automatic : mode == "on" ? Mode::On : Mode::Off;
        evaluate();
        addEventLog("CH" + String(id + 1) + " modo " + mode);
        showTemporaryLcdLine3("CH" + String(id + 1) + " modo " + mode);
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/time", HTTP_POST, [] {
        if (!authorized()) return;
        StaticJsonDocument<256> doc;
        if (!body(doc, 128)) return;
        if (!doc["epoch"].is<int64_t>()) {
            jsonError(400, "Timestamp UTC obrigatorio.");
            return;
        }
        const int64_t epoch = doc["epoch"];
        if (epoch < MIN_TIME || epoch >= MAX_TIME) {
            jsonError(400, "Horario fora do intervalo 2024-2037.");
            return;
        }
        timeval value{static_cast<time_t>(epoch), 0};
        if (settimeofday(&value, nullptr) != 0) {
            jsonError(500, "Nao foi possivel ajustar o relogio.");
            return;
        }
        clockSource = "navegador (NTP pode reajustar)";
        manualClock = true;
        lastClockCheck = millis() - 1000;
        addEventLog("Relogio ajustado pelo navegador.");
        showTemporaryLcdLine3("Hora pelo navegador");
        evaluate();
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.onNotFound([] { jsonError(404, "Rota nao encontrada."); });
    server.begin();
    webReady = true;
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    Serial.printf("Painel: http://%s / usuario admin\n",
                  WiFi.localIP().toString().c_str());
}

void setup() {
    for (uint8_t i = 0; i < RELAY_COUNT; ++i) {
        digitalWrite(RELAY_PINS[i], !RELAY_ON_LEVEL);
        pinMode(RELAY_PINS[i], OUTPUT);
        snprintf(relays[i].name, sizeof(relays[i].name), "Rele %u", i + 1);
    }
    pinMode(ACK_BUTTON_PIN, INPUT_PULLUP);
    pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);
    Serial.begin(115200);
    addEventLog("Boot: Arvore Natal v1.0.0; reset=" + String(static_cast<int>(esp_reset_reason())));
    const bool i2cStarted = Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (i2cStarted) Wire.setTimeOut(50);
    const LcdScanResult lcdScan = i2cStarted ? detectLcd([](uint8_t address) {
        Wire.beginTransmission(address);
        return Wire.endTransmission();
    }) : LcdScanResult{LcdScanStatus::BusError, 0, 0};
    if (lcdScan.status == LcdScanStatus::Found) {
        lcdAddress = lcdScan.address;
        lcd = LiquidCrystal_I2C(lcdAddress, LCD_COLUMNS, LCD_ROWS);
        lcd.init();
        lcd.clear();
        lcdReady = true;
        lcdDiagnostic = "Endereco detectado; dimensoes 20x4 configuradas";
        addEventLog("LCD I2C detectado em 0x" + String(lcdAddress, HEX) + "; tamanho configurado 20x4");
        requestLcdBacklight(15000);
        lcdPrintLine(0, "Arvore Natal v1.0.0");
        lcdPrintLine(1, "LCD 2004 I2C 0x" + String(lcdAddress, HEX));
        lcdPrintLine(2, "I2C SDA:" + String(I2C_SDA_PIN) + " SCL:" + String(I2C_SCL_PIN));
        lcdPrintLine(3, "Aguarde Wi-Fi...");
    } else {
        if (!i2cStarted) lcdDiagnostic = "Falha ao iniciar barramento I2C";
        else if (lcdScan.status == LcdScanStatus::NotFound)
            lcdDiagnostic = "LCD nao respondeu em 0x23, 0x27 ou 0x3f";
        else if (lcdScan.status == LcdScanStatus::Multiple)
            lcdDiagnostic = "Multiplos enderecos de LCD responderam; inicializacao bloqueada";
        else
            lcdDiagnostic = "Erro I2C em 0x" + String(lcdScan.address, HEX) +
                            "; codigo " + String(lcdScan.error);
        addEventLog("ERRO LCD: " + lcdDiagnostic);
    }
    storageReady = storage.begin("arvore-natal", false);
    if (!storageReady) Serial.println("ERRO: NVS indisponivel. Configuracoes nao podem ser salvas.");
    filesystemReady = SPIFFS.begin(false);
    if (!filesystemReady) Serial.println("ERRO: SPIFFS indisponivel. Execute uploadfs.");
    otaLanPassword = storageReady ? storage.getString("ota-pass", "") : "";
    if (otaLanPassword.isEmpty()) {
        char generated[17];
        snprintf(generated, sizeof(generated), "%08lx%08lx",
                 static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
        otaLanPassword = generated;
        if (!storageReady || storage.putString("ota-pass", otaLanPassword) != otaLanPassword.length()) {
            Serial.println("ERRO: senha OTA LAN temporaria; sera alterada no proximo boot.");
        }
    }
    Serial.printf("\nArvore de Natal v1.0.0\nUsuario do painel: admin\nPortal Wi-Fi: %s\n", AP_NAME);
    const String saved = storageReady ? storage.getString("config", "") : "";
    if (!saved.isEmpty()) {
        DynamicJsonDocument doc(JSON_CAPACITY);
        RelayConfig candidate[RELAY_COUNT];
        int offset;
        String error;
        const DeserializationError parseError = deserializeJson(doc, saved);
        if (parseError || !readConfiguration(doc, candidate, offset, error)) {
            configHealthy = false;
            Serial.printf("ERRO: agenda invalida; automatico bloqueado: %s %s\n",
                          parseError.c_str(), error.c_str());
        } else {
            for (uint8_t i = 0; i < RELAY_COUNT; ++i) relays[i] = candidate[i];
            utcOffsetMinutes = offset;
        }
    }
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    wifiManager.setConfigPortalTimeout(120);
    wifiManager.setAPCallback([](WiFiManager*) {
        wifiConfigPortalActive = true;
        requestLcdBacklight(20000);
        lcdPrintLine(1, "Wi-Fi: config. ativa");
        lcdPrintLine(2, "Conecte-se ao AP:");
        lcdPrintLine(3, AP_NAME);
        addEventLog("Portal Wi-Fi ativo: 192.168.4.1");
    });
    wifiManager.setSaveConfigCallback([] {
        requestLcdBacklight();
        Serial.println("Credenciais Wi-Fi recebidas pelo portal.");
    });
    wifiManager.setConfigPortalBlocking(false);
    WiFi.mode(WIFI_STA);
    // O portal e o painel compartilham a porta 80; so inicia o painel apos conectar.
    wifiManager.autoConnect(AP_NAME, AP_PASSWORD);
    const uint32_t wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
        wifiManager.process();
        checkAckButton();
        checkWifiReset();
        applyLcdBacklightPolicy();
        if (millis() - wifiStart >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("Falha na conexao Wi-Fi apos 120 segundos. Reiniciando...");
            lcdPrintLine(3, "Falha Wi-Fi: reboot");
            ESP.restart();
        }
        delay(10);
    }
    // Na conexao direta, WiFiManager nao cria os servidores do portal.
    if (wifiManager.getConfigPortalActive()) wifiManager.stopConfigPortal();
    wifiConfigPortalActive = false;
    previousWifiConnected = true;
    addEventLog("Wi-Fi conectado IP=" + WiFi.localIP().toString());
    requestLcdBacklight(15000);
    ntp.setTimeOffset(utcOffsetMinutes * 60);
    ntp.begin();
    updateClock();
    startWeb();
    startOta();
    updateLcdStatus();
}

void loop() {
    if (otaRestartAt && static_cast<int32_t>(millis() - otaRestartAt) >= 0) ESP.restart();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    if (wifiConnected != previousWifiConnected) {
        previousWifiConnected = wifiConnected;
        addEventLog(wifiConnected ? "Wi-Fi reconectado IP=" + WiFi.localIP().toString() :
                    "Wi-Fi desconectado; mantendo relogio e agendas.");
        showTemporaryLcdLine3(wifiConnected ? "Wi-Fi reconectado" : "Wi-Fi desconectado");
    }
    // Update e compartilhado: nunca processe ArduinoOTA durante uma gravacao Web.
    if (!otaWebTransfer && !otaRestartAt) ArduinoOTA.handle();
    if (webReady) server.handleClient();
    if (otaActive && otaWebTransfer && millis() - otaLastActivity >= 30000) {
        failOta("Tempo esgotado aguardando dados do navegador.");
        webUploadSeen = false;
        webUploadComplete = false;
        webUploadAuthorized = false;
    }
    if (!otaLocked && millis() - lastClockCheck >= 1000) {
        lastClockCheck = millis();
        updateClock();
    }
    if (millis() - lastEvaluation >= 250) {
        lastEvaluation = millis();
        evaluate();
    }
    if (millis() - lastLcdUpdate >= 1000) {
        lastLcdUpdate = millis();
        updateLcdStatus();
    }
    checkAckButton();
    checkWifiReset();
    delay(2);
}
