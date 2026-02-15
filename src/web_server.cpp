#include "web_server.h"
#include <SD.h>
#include <SPIFFS.h>

// ========== Simple Server (Normal Mode) ==========

SimpleServer::SimpleServer() : server(80) {
}

void SimpleServer::begin() {
    server.begin();
    Serial.println("Simple HTTP server started on port 80");
}

void SimpleServer::setConfigManager(ConfigManager* mgr) {
    configMgr = mgr;
}

void SimpleServer::setAudioPlayer(AudioPlayer* player) {
    audioPlayer = player;
}

void SimpleServer::handle() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 5000) {
        Serial.println("[WEB] Server handle() running...");
        lastCheck = millis();
    }

    WiFiClient client = server.available();
    if (!client) {
        return;
    }

    Serial.println("[WEB] Client connected!");

    String request = "";
    while (client.connected()) {
        if (client.available()) {
            char c = client.read();
            request += c;

            if (c == '\n' && request.endsWith("\r\n\r\n")) {
                break;
            }
        }
    }

    Serial.print("[WEB] Request: ");
    Serial.println(request.substring(0, 50));

    // Check for playback requests
    if (request.indexOf("GET /play/") >= 0) {
        int buttonId = -1;
        for (int i = 0; i < 8; i++) {
            if (request.indexOf("GET /play/" + String(i)) >= 0) {
                buttonId = i;
                break;
            }
        }

        if (buttonId >= 0 && audioPlayer) {
            String filepath = configMgr->getButtonFile(buttonId);

            Serial.println("=== Play Request ===");
            Serial.print("Button: ");
            Serial.println(buttonId);
            Serial.print("File: ");
            Serial.println(filepath);

            Serial.println("[DEBUG] Checking BT connection...");
            // Check Bluetooth connection first
            if (!audioPlayer->isConnected()) {
                client.println("HTTP/1.1 503 Service Unavailable");
                client.println("Content-Type: text/plain");
                client.println("Connection: close");
                client.println();
                client.println("Bluetooth not connected");
                client.stop();
                Serial.println("ERROR: Bluetooth not connected!");
                return;
            }

            Serial.println("[DEBUG] BT connected, checking file...");
            // Validate file exists before committing to play
            bool canPlay = (filepath.length() > 0 && SD.exists(filepath));
            Serial.print("[DEBUG] File exists: ");
            Serial.println(canPlay ? "YES" : "NO");

            Serial.println("[DEBUG] Sending HTTP response...");
            // Send HTTP response FIRST, before starting playback
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println(canPlay ? "Playing" : "Error");
            client.flush();
            client.stop();

            Serial.println("[DEBUG] HTTP response sent");
            // NOW start playback after HTTP response is sent
            if (canPlay) {
                Serial.println("[DEBUG] Stopping current playback...");
                // Stop current playback
                if (audioPlayer->isPlaying()) {
                    audioPlayer->stop();
                }

                Serial.println("[DEBUG] Calling playFile()...");
                // Play new file
                bool success = audioPlayer->playFile(filepath);
                Serial.print("Play result: ");
                Serial.println(success ? "SUCCESS" : "FAILED");
            } else {
                Serial.println("ERROR: File not found or not configured");
            }

            return;
        }
    }

    // Check if settings page was requested
    if (request.indexOf("GET /settings") >= 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Connection: close");
        client.println();
        client.println("<html><body><h1>Switching to Settings Mode...</h1></body></html>");
        client.stop();

        delay(500);
        configMgr->enterSettingsMode();
        return;
    }

    // Default response - Playback UI
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<html><head><style>");
    client.println("body{font-family:Arial;text-align:center;padding:20px;background:#1a1a1a;color:#fff}");
    client.println("h1{color:#4CAF50;margin-bottom:10px}");
    client.println(".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:15px;max-width:800px;margin:20px auto}");
    client.println(".btn{padding:40px 20px;font-size:16px;border:2px solid;border-radius:8px;cursor:pointer;transition:all 0.3s}");
    client.println(".btn:hover{transform:scale(1.05)}");
    client.println(".settings{background:#2196F3;color:white;padding:15px 30px;border:none;border-radius:5px;margin-top:20px}");
    client.println(".settings:hover{background:#1976D2}");
    client.println(".status{margin:15px 0;font-size:14px;color:#888}");
    client.println("</style></head><body>");
    client.println("<h1>Jingle Machine</h1>");

    // BT Status
    if (audioPlayer) {
        if (audioPlayer->isConnected()) {
            client.println("<div class='status'>Bluetooth Connected</div>");
        } else {
            client.println("<div class='status'>Bluetooth Disconnected</div>");
        }
    }

    client.println("<div class='grid'>");

    // Generate buttons from config
    Serial.println("[DEBUG] Loading config for buttons...");
    JsonDocument config = configMgr->getConfig();
    JsonArray buttons = config["buttons"].as<JsonArray>();
    Serial.print("[DEBUG] Number of buttons: ");
    Serial.println(buttons.size());
    int idx = 0;
    for (JsonVariant btnVar : buttons) {
        if (idx >= 8) break;
        JsonObject btn = btnVar.as<JsonObject>();

        String label = btn["label"].as<String>();
        String color = btn["color"].as<String>();

        client.print("<button class='btn' style='background:");
        client.print(color);
        client.print(";color:white;border-color:");
        client.print(color);
        client.print("' onclick=\"play(");
        client.print(idx);
        client.print(")\">");
        client.print(label);
        client.println("</button>");

        idx++;
    }

    client.println("</div>");
    client.println("<button class='settings' onclick=\"location.href='/settings'\">Settings Mode</button>");
    client.println("<script>");
    client.println("function play(id){fetch('/play/'+id).then(r=>r.text()).then(t=>console.log(t))}");
    client.println("</script>");
    client.println("</body></html>");

    client.stop();
}

// ========== Settings Server (Settings Mode) ==========

SettingsServer::SettingsServer() : server(80) {
}

void SettingsServer::begin(ConfigManager* mgr) {
    configMgr = mgr;

    Serial.println("Initializing SPIFFS...");
    if (!SPIFFS.begin(false)) {
        Serial.println("SPIFFS mount failed! Trying to format...");
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS format failed!");
        } else {
            Serial.println("SPIFFS formatted successfully");
        }
    } else {
        Serial.println("SPIFFS mounted successfully");

        // Test writing a file
        File testFile = SPIFFS.open("/test.txt", "w");
        if (testFile) {
            testFile.println("Hello from SPIFFS!");
            testFile.close();
            Serial.println("Test file written");
        }
    }

    Serial.println("Setting up routes...");
    setupRoutes();

    Serial.println("Starting ElegantOTA...");
    ElegantOTA.begin(&server);  // Async mode enabled via build flag

    Serial.println("Starting AsyncWebServer...");
    server.begin();

    Serial.println("Settings server started on port 80");
}

// Embedded HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Jingle Machine Settings</title>
<style>
body{font-family:Arial;margin:0;padding:20px;background:#1a1a1a;color:#fff}
.container{max-width:900px;margin:0 auto}
h1{color:#4CAF50;text-align:center}
.card{background:#2a2a2a;padding:20px;margin:20px 0;border-radius:8px}
h2{color:#fff;margin-top:0}
.form-group{margin:15px 0}
label{display:block;margin-bottom:5px;color:#aaa}
input,select{width:100%;padding:10px;background:#1a1a1a;border:1px solid #444;color:#fff;border-radius:4px;box-sizing:border-box}
.btn-primary,.btn-secondary,.btn-warning{padding:12px 24px;border:none;border-radius:4px;cursor:pointer;font-size:16px;margin:5px}
.btn-primary{background:#4CAF50;color:#fff}
.btn-secondary{background:#2196F3;color:#fff}
.btn-warning{background:#FF9800;color:#fff}
.btn-small{padding:8px 16px;font-size:14px}
.status{margin:20px 0;padding:10px;border-radius:4px;text-align:center}
.button-config{display:grid;grid-template-columns:50px 1fr 1fr 80px;gap:10px;align-items:center;margin:10px 0}
.color-preview{width:40px;height:40px;border-radius:4px;border:2px solid #444}
#btDevices{margin-top:10px}
.bt-device{padding:8px;margin:5px 0;background:#1a1a1a;border:1px solid #444;border-radius:4px;cursor:pointer}
.bt-device:hover{background:#333}
</style>
</head><body>
<div class="container">
<h1>Jingle Machine Settings</h1>
<div class="card">
<h2>Bluetooth Configuration</h2>
<div class="form-group">
<label>Device Name:</label>
<div style="display:flex;gap:10px">
<input type="text" id="btDevice" placeholder="JBL Flip 5" style="flex:1">
<button class="btn-secondary btn-small" onclick="scanBT()">Scan</button>
</div>
<div id="btDevices"></div>
</div>
<div class="form-group">
<label>Volume (0-127):</label>
<input type="number" id="btVolume" min="0" max="127" value="80">
</div>
<button class="btn-secondary" onclick="saveBT()">Save Bluetooth</button>
</div>
<div class="card">
<h2>Button Configuration</h2>
<div id="buttons"></div>
<button class="btn-secondary" onclick="saveButtons()">Save Buttons</button>
</div>
<div class="card">
<h2>File Upload</h2>
<div class="form-group">
<label>Upload Audio Files (WAV):</label>
<input type="file" id="fileInput" multiple accept=".wav">
<small style="color:#888">Only WAV files supported (44.1kHz, 16-bit, mono/stereo)</small>
</div>
<button class="btn-primary" onclick="uploadFiles()">Upload Files</button>
<div id="fileList" style="margin-top:10px"></div>
</div>
<div class="card">
<a href="/update" class="btn-primary">Firmware Update</a>
<button class="btn-warning" onclick="exitSettings()">Exit Settings</button>
</div>
<div id="status" class="status"></div>
</div>
<script>
let config={buttons:[{label:'Btn1',file:'',color:'#4CAF50'},{label:'Btn2',file:'',color:'#2196F3'},{label:'Btn3',file:'',color:'#FF9800'},{label:'Btn4',file:'',color:'#F44336'},{label:'Btn5',file:'',color:'#9C27B0'},{label:'Btn6',file:'',color:'#00BCD4'},{label:'Btn7',file:'',color:'#FFEB3B'},{label:'Btn8',file:'',color:'#795548'}]};
async function loadConfig(){
try{
const r=await fetch('/api/config');
if(r.ok){
const c=await r.json();
if(c&&c.buttons)config=c;
}
}catch(e){console.error(e);}
document.getElementById('btDevice').value=config.btDevice||'';
document.getElementById('btVolume').value=config.btVolume||80;
renderButtons();
loadFiles();
}
function renderButtons(){
const html=config.buttons.map((b,i)=>`
<div class="button-config">
<div>${i+1}</div>
<input type="text" id="label${i}" value="${b.label}" placeholder="Label">
<select id="file${i}"></select>
<input type="color" id="color${i}" value="${b.color}">
</div>`).join('');
document.getElementById('buttons').innerHTML=html;
loadFiles();
}
async function loadFiles(){
try{
const r=await fetch('/api/files');
if(!r.ok)return;
const files=await r.json();
for(let i=0;i<8;i++){
const sel=document.getElementById('file'+i);
if(sel){
sel.innerHTML='<option value="">None</option>'+files.map(f=>`<option value="/jingles/${f}" ${config.buttons[i]&&config.buttons[i].file==='/jingles/'+f?'selected':''}>${f}</option>`).join('');
}
}
const fileList=document.getElementById('fileList');
if(fileList)fileList.innerHTML='<strong>Files on SD:</strong><br>'+(files.length?files.join('<br>'):'No files');
}catch(e){console.error(e);}
}
async function saveBT(){
config.btDevice=document.getElementById('btDevice').value;
config.btVolume=parseInt(document.getElementById('btVolume').value);
await saveConfig();
}
async function saveButtons(){
for(let i=0;i<8;i++){
config.buttons[i].label=document.getElementById('label'+i).value;
config.buttons[i].file=document.getElementById('file'+i).value;
config.buttons[i].color=document.getElementById('color'+i).value;
}
await saveConfig();
}
async function saveConfig(){
const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(config)});
showStatus(r.ok?'Saved!':'Error',r.ok?'#4CAF50':'#f44336');
}
async function scanBT(){
showStatus('Scanning for Bluetooth devices...','#2196F3');
try{
const r=await fetch('/api/scan');
const devices=await r.json();
const html=devices.map(d=>`<div class="bt-device" onclick="selectDevice('${d.name}')">${d.name}<br><small>${d.address}</small></div>`).join('');
document.getElementById('btDevices').innerHTML=html||'<p>No devices found. Make sure your device is in pairing mode.</p>';
showStatus('Scan complete','#4CAF50');
}catch(e){
showStatus('Scan failed','#f44336');
}
}
function selectDevice(name){
document.getElementById('btDevice').value=name;
document.getElementById('btDevices').innerHTML='';
showStatus('Device selected: '+name,'#4CAF50');
}
async function uploadFiles(){
const files=document.getElementById('fileInput').files;
if(!files.length){showStatus('No files selected','#f44336');return;}
const formData=new FormData();
for(let f of files)formData.append('files',f);
showStatus('Uploading...','#2196F3');
const r=await fetch('/api/upload',{method:'POST',body:formData});
showStatus(r.ok?'Uploaded!':'Upload failed',r.ok?'#4CAF50':'#f44336');
if(r.ok)loadFiles();
document.getElementById('fileInput').value='';
}
async function exitSettings(){
await fetch('/api/exit',{method:'POST'});
showStatus('Rebooting to Normal Mode...','#FF9800');
}
function showStatus(msg,color){
const s=document.getElementById('status');
s.textContent=msg;
s.style.background=color;
setTimeout(()=>s.textContent='',3000);
}
loadConfig();
</script>
</body></html>
)rawliteral";

void SettingsServer::setupRoutes() {
    // Serve embedded HTML
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html);
    });

    // Debug endpoint to check SPIFFS status
    server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><body><h1>Debug Info</h1>";

        // Try to begin SPIFFS again
        html += "<p>Attempting SPIFFS.begin(): ";
        bool mounted = SPIFFS.begin(false);
        html += mounted ? "SUCCESS" : "FAILED";
        html += "</p>";

        if (mounted) {
            html += "<p>Total bytes: " + String(SPIFFS.totalBytes()) + "</p>";
            html += "<p>Used bytes: " + String(SPIFFS.usedBytes()) + "</p>";

            html += "<h2>Testing specific files:</h2><ul>";

            File f1 = SPIFFS.open("/index.html", "r");
            if (f1) {
                html += "<li>/index.html EXISTS (" + String(f1.size()) + " bytes)</li>";
                f1.close();
            } else {
                html += "<li>/index.html NOT FOUND</li>";
            }

            File f2 = SPIFFS.open("/main.js", "r");
            if (f2) {
                html += "<li>/main.js EXISTS (" + String(f2.size()) + " bytes)</li>";
                f2.close();
            } else {
                html += "<li>/main.js NOT FOUND</li>";
            }

            File f3 = SPIFFS.open("/style.css", "r");
            if (f3) {
                html += "<li>/style.css EXISTS (" + String(f3.size()) + " bytes)</li>";
                f3.close();
            } else {
                html += "<li>/style.css NOT FOUND</li>";
            }

            File f4 = SPIFFS.open("/test.txt", "r");
            if (f4) {
                html += "<li>/test.txt EXISTS (" + String(f4.size()) + " bytes) - WRITTEN FROM CODE</li>";
                f4.close();
            } else {
                html += "<li>/test.txt NOT FOUND</li>";
            }

            html += "</ul>";
        } else {
            html += "<p>Trying with format flag...</p>";
            if (SPIFFS.begin(true)) {
                html += "<p>Format succeeded, but filesystem is now empty</p>";
            } else {
                html += "<p>Format also failed - hardware issue?</p>";
            }
        }

        html += "</body></html>";
        request->send(200, "text/html", html);
    });


    // API: Get config
    server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String json;
        serializeJson(configMgr->getConfig(), json);
        request->send(200, "application/json", json);
    });

    // API: Save config
    server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest *request) {},
              NULL,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static String jsonBuffer;

        if (index == 0) {
            jsonBuffer = "";
        }

        for (size_t i = 0; i < len; i++) {
            jsonBuffer += (char)data[i];
        }

        if (index + len == total) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, jsonBuffer);

            if (error) {
                request->send(400, "text/plain", "Invalid JSON");
            } else {
                configMgr->saveConfig(doc);
                request->send(200, "text/plain", "Config saved");
            }
        }
    });

    // API: File upload
    server.on("/api/upload", HTTP_POST,
              [](AsyncWebServerRequest *request) {
                  request->send(200, "text/plain", "Files uploaded");
              },
              [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
                  handleFileUpload(request, filename, index, data, len, final);
              });

    // API: Scan for Bluetooth devices
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "[";
        // Note: Full BT scanning requires more complex implementation
        // For now, return a placeholder that indicates scanning is in progress
        json += "{\"name\":\"Scanning...\",\"address\":\"00:00:00:00:00:00\"}";
        json += "]";
        request->send(200, "application/json", json);
    });

    // API: Exit settings mode
    server.on("/api/exit", HTTP_POST, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Exiting Settings Mode...");
        delay(500);
        configMgr->exitSettingsMode();
    });

    // API: List files on SD card
    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *request) {
        File root = SD.open("/jingles");
        if (!root || !root.isDirectory()) {
            request->send(404, "text/plain", "Directory not found");
            return;
        }

        String fileList = "[";
        File file = root.openNextFile();
        bool first = true;
        while (file) {
            if (!file.isDirectory()) {
                if (!first) fileList += ",";
                fileList += "\"" + String(file.name()) + "\"";
                first = false;
            }
            file = root.openNextFile();
        }
        fileList += "]";

        request->send(200, "application/json", fileList);
    });
}

void SettingsServer::handleFileUpload(AsyncWebServerRequest *request, String filename,
                                     size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;

    if (index == 0) {
        Serial.printf("Upload Start: %s\n", filename.c_str());

        // Ensure jingles directory exists
        if (!SD.exists("/jingles")) {
            SD.mkdir("/jingles");
        }

        uploadFile = SD.open("/jingles/" + filename, FILE_WRITE);
        if (!uploadFile) {
            Serial.println("Failed to open file for writing");
            return;
        }
    }

    if (uploadFile) {
        uploadFile.write(data, len);
    }

    if (final) {
        uploadFile.close();
        Serial.printf("Upload Complete: %s (%d bytes)\n", filename.c_str(), index + len);
    }
}
