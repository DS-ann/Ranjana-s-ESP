#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <BluetoothSerial.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

using namespace websockets;

// ===== WiFi =====
const char* ssidList[] = {"SSID_1","SSID_2","SSID_3","SSID_4"};
const char* passwordList[] = {"PASS_1","PASS_2","PASS_3","PASS_4"};
const int numNetworks = 4;

// ===== Server =====
const char* WS_SERVER = "ws://YOUR_SERVER_IP_OR_DOMAIN:3000"; // Match HTML URL
WebsocketsClient ws;

// ===== Device ID =====
String deviceID = String((uint32_t)ESP.getEfuseMac(), HEX);

// ===== Relays =====
#define NUM_RELAYS 8
const int relayPins[NUM_RELAYS] = {2,4,5,18,19,21,22,23};
bool relayState[NUM_RELAYS];
unsigned long relayTimers[NUM_RELAYS];
unsigned long relayUsage[NUM_RELAYS];
unsigned long relayStartTime[NUM_RELAYS];

// ===== Preferences =====
Preferences preferences;

// ===== Bluetooth =====
BluetoothSerial SerialBT;

// ===== Voice Task =====
TaskHandle_t VoiceTaskHandle;
void VoiceTask(void * pvParameters){
  for(;;){ vTaskDelay(1000 / portTICK_PERIOD_MS); }
}

// ===== WiFi connection =====
void connectWiFi(){
  for(int i=0;i<numNetworks;i++){
    WiFi.begin(ssidList[i],passwordList[i]);
    Serial.print("Connecting to "); Serial.println(ssidList[i]);
    unsigned long startAttempt = millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-startAttempt<10000){
      delay(500); Serial.print(".");
    }
    if(WiFi.status()==WL_CONNECTED){
      Serial.println("Connected to "+String(ssidList[i]));
      return;
    }
  }
  Serial.println("Retry WiFi...");
}

// ===== Save & Load =====
void saveState(){
  preferences.begin("relayState",false);
  char key[6];
  for(int i=0;i<NUM_RELAYS;i++){
    sprintf(key,"r%d",i); preferences.putBool(key,relayState[i]);
    sprintf(key,"t%d",i); preferences.putULong(key,relayTimers[i]);
    sprintf(key,"u%d",i); preferences.putULong(key,relayUsage[i]);
  }
  preferences.end();
}

void loadState(){
  preferences.begin("relayState",true);
  char key[6];
  for(int i=0;i<NUM_RELAYS;i++){
    sprintf(key,"r%d",i); relayState[i] = preferences.getBool(key,false);
    sprintf(key,"t%d",i); relayTimers[i] = preferences.getULong(key,0);
    sprintf(key,"u%d",i); relayUsage[i] = preferences.getULong(key,0);

    pinMode(relayPins[i],OUTPUT);
    digitalWrite(relayPins[i],relayState[i]?HIGH:LOW);
    if(relayState[i] && relayTimers[i]>0) relayStartTime[i] = millis();
  }
  preferences.end();
}

// ===== Relay update =====
void updateRelay(int id,bool state){
  if(id<0 || id>=NUM_RELAYS) return;
  if(!ws.available()) return; // Prevent toggle if ESP offline

  if(state && !relayState[id]) relayStartTime[id] = millis();
  if(!state && relayState[id]) relayUsage[id] += millis() - relayStartTime[id];

  relayState[id] = state;
  digitalWrite(relayPins[id],state?HIGH:LOW);
  saveState();

  // WebSocket update
  DynamicJsonDocument doc(256);
  doc["device"] = deviceID;
  doc["relay"] = id;
  doc["state"] = state;
  String out;
  serializeJson(doc,out);
  ws.send(out);
}

// ===== Timer check =====
void checkTimers(){
  static unsigned long lastCheck=0;
  unsigned long now=millis();
  if(now-lastCheck>=1000){
    lastCheck = now;
    for(int i=0;i<NUM_RELAYS;i++){
      if(relayTimers[i]>0){
        if(relayTimers[i]<=1000){
          updateRelay(i,false);
          relayTimers[i]=0;
        }else relayTimers[i]-=1000;
      }
    }
    saveState();
  }
}

// ===== WebSocket =====
void sendStatus(){
  if(!ws.available()) return;
  DynamicJsonDocument doc(1024);
  doc["type"] = "status";
  doc["device"] = deviceID;
  JsonArray rel = doc.createNestedArray("relays");
  for(int i=0;i<NUM_RELAYS;i++) rel.add(relayState[i]);

  JsonArray usageArr = doc.createNestedArray("usageStats");
  for(int i=0;i<NUM_RELAYS;i++){
    JsonObject u = usageArr.createNestedObject();
    u["last"] = "--";
    u["today"] = relayUsage[i]/60000;
    u["total"] = relayUsage[i]/60000;
  }

  doc["wifiNum"] = 1; // Optional, can send actual SSID index
  doc["rssi"] = WiFi.RSSI();

  String out;
  serializeJson(doc,out);
  ws.send(out);
}

void onMessage(WebsocketsMessage msg){
  DynamicJsonDocument doc(512);
  deserializeJson(doc,msg.data());
  const char* type = doc["type"];
  if(strcmp(type,"toggle")==0){
    int id = doc["relay"];
    bool state = doc["state"];
    updateRelay(id,state);
  }else if(strcmp(type,"setTimer")==0){
    int id = doc["id"];
    unsigned long sec = doc["sec"];
    relayTimers[id] = sec*1000;
  }
}

void ensureWS(){
  static unsigned long lastReconnect=0;
  if(WiFi.status()!=WL_CONNECTED) return;
  if(!ws.available() && millis()-lastReconnect>5000){
    ws.connect(WS_SERVER);
    ws.send("{\"type\":\"espInit\"}");
    lastReconnect = millis();
  }
}

// ===== Setup =====
void setup(){
  Serial.begin(115200);
  loadState();
  connectWiFi();

  SerialBT.begin("ESP32_SmartHome");
  ws.onMessage(onMessage);
  ws.connect(WS_SERVER);
  ws.send("{\"type\":\"espInit\"}");

  xTaskCreatePinnedToCore(VoiceTask,"VoiceTask",4096,NULL,1,&VoiceTaskHandle,1);
}

// ===== Loop =====
void loop(){
  ws.poll();
  ensureWS();

  if(SerialBT.available()){
    String cmd = SerialBT.readStringUntil('\n');
    cmd.trim();
    int sep = cmd.indexOf(':');
    if(sep>0){
      int id = cmd.substring(0,sep).toInt();
      int state = cmd.substring(sep+1).toInt();
      updateRelay(id,state!=0);
    }
  }

  if(WiFi.status()!=WL_CONNECTED){ connectWiFi(); delay(500); }

  static unsigned long lastStatus=0;
  if(millis()-lastStatus>=1000){ sendStatus(); lastStatus=millis(); }

  checkTimers();
  delay(50);
}