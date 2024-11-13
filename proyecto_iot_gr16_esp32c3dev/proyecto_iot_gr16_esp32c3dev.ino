#include "DHTesp.h"
#define rgbLedWrite neopixelWrite //se sustituye el rgbledwrite por neopixelwrite ya que la version no es la mas reciente
#include <string>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPUpdate.h>
#include <HTTPClient.h>

// URL para actualización
// #define OTA_URL  "http://172.16.53.128:1880/esp32-ota/update"// Address of OTA update server
#define OTA_URL "https://iot.ac.uma.es:1880/esp8266-ota/update"

// Para versiones del IDE < 2.0 poner en FW_BOARD_NAME el identificador de la placa, por ejemplo ".nodemcu",
//      ver nombre del binario generado para conocer identificador utilizado
// Para versiones del IDE >= 2.0 dejar vacío
#define FW_BOARD_NAME ""
//#define __FILE__ "C:\Users\practicas\Desktop\FOTA tarea grupo\actualiza_OTA_ESP32.ino"
// Nombre del firmware (válido para linux/macos '/' y Windows '\\')
#define HTTP_OTA_VERSION      String(__FILE__).substring(String(__FILE__).lastIndexOf((String(__FILE__).lastIndexOf('/'))?'/':'\\')+1) + FW_BOARD_NAME

// definimos macro para indicar función y línea de código en los messages
#define DEBUG_STRING "["+String(__FUNCTION__)+"():"+String(__LINE__)+"] "

WiFiClient wClient;
PubSubClient mqtt_client(wClient);
DHTesp dht;

//connections data
const String ssid = "infind";
const String password = "1518wifi";

const String server = "iot.ac.uma.es";
const String user = "II16";
const String pass = "PvC1pkk8";


//id and mqtt topics
String ID_PLACA;
String CHIPID;
String topic_connection;
String topic_data;
String topic_config;
String topic_brightness_get;
String topic_brightness_set;
String topic_color_get;
String topic_color_set;
String topic_switch_get;
String topic_switch_set;
String topic_fota;


//other CONST data
#define PIN_DHT 5



//state variables
float hum;
float temp;
unsigned long uptime;

//VARIABLES FROM */config
int sendDataPeriod = 5*60; //period of sending */datos in seconds
int checkUpdatePeriod; //period of checking OTA in seconds -- if=0, then no periodic check
int LEDChangePeriod; //period of changing ±1% in LED Brightness, in miliseconds
bool LEDOn = true; //LED state interruptor - onn/off (GPIO5) -- same also in */switch/cmd?

//VARIABLES FROM */switch/cmd
int LEDOnID;
// bool LEDOn = true; duplicated?

//VARIABLES FROM */led/brillo
int aimLEDBrightness = 100; //wanted LED brightness, in range 0-100
int LEDBrightnessID;
String LEDBrightnessOrigin; //the origin, from where the order came = "pulsador"||"mqtt"

//VARIABLES FROM */led/color
int r = 255; //current RED value for LED
int g = 255; //current GREEN value for LED
int b = 255; //current BLUE value for LED
int LEDColorID;

//OTHER LED STATE VARIABLES
int currLEDBrightness = aimLEDBrightness; //current LED Brightness, in range 0-100
bool isLEDOn = true; //LED state interruptor - onn/off (GPIO5).

//PERODIC FUNCTIONS VARIABLES
unsigned long last_data_msg = 0;
unsigned long last_LEDBrightness_change = 0;





//-----------------------------------------------------
// funciones para progreso de OTA
//-----------------------------------------------------
void inicio_OTA(){ Serial.println(DEBUG_STRING+"Nuevo Firmware encontrado. Actualizando..."); }
void final_OTA() { Serial.println(DEBUG_STRING+"Fin OTA. Reiniciando..."); }
void error_OTA(int e){ Serial.println(DEBUG_STRING+"ERROR OTA: "+String(e)+" "+httpUpdate.getLastErrorString()); }
void progreso_OTA(int x, int todo){
  int progreso=(int)((x*100)/todo);
  String espacio = (progreso<10)? "  ":(progreso<100)? " " : "";
  if(progreso%10==0) Serial.println(DEBUG_STRING+"Progreso: "+espacio+String(progreso)+"% - "+String(x/1024)+"K de "+String(todo/1024)+"K");
}
//-----------------------------------------------------
void intenta_OTA(){ 
  Serial.println( "---------------------------------------------" );  
  Serial.print  ( DEBUG_STRING+"MAC de la placa: "); Serial.println(WiFi.macAddress());
  Serial.println( DEBUG_STRING+"Comprobando actualización:" );
  Serial.println( DEBUG_STRING+"URL: "+OTA_URL );
  Serial.println( "---------------------------------------------" );  
  httpUpdate.setLedPin(RGB_BUILTIN);
  httpUpdate.onStart(inicio_OTA);
  httpUpdate.onError(error_OTA);
  httpUpdate.onProgress(progreso_OTA);
  httpUpdate.onEnd(final_OTA);

  WiFiClient wClient;

  switch(httpUpdate.update(wClient, OTA_URL, HTTP_OTA_VERSION)) {
    case HTTP_UPDATE_FAILED:
      Serial.println(DEBUG_STRING+"HTTP update failed: Error ("+String(httpUpdate.getLastError())+"): "+httpUpdate.getLastErrorString());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(DEBUG_STRING+"El dispositivo ya está actualizado");
      break;
    case HTTP_UPDATE_OK:
      Serial.println(DEBUG_STRING+"OK");
      break;
    }
}
//-----------------------------------------------------
//-----------------------------------------------------




//WIFI connection
void connect_wifi() {
  Serial.println("Connecting to " + ssid);
 
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected, IP address: " + WiFi.localIP().toString());
}





//-----------------------------------------------------
//MQTT
//-----------------------------------------------------
//MQTT connection
void connect_mqtt() {
  // Loop until we're reconnected
  String willTopic = topic_connection;  // El tópico de LWT (conexion_topic)
  String willMessage = "{\"CHIPID\" : \"esp32c3-" + CHIPID + "\", \"online\" : false}";  // message de LWT para indicar desconexión
  int willQoS = 1;  // QoS del message de LWT (nivel de calidad)
  bool willRetain = true; 
  while (!mqtt_client.connected()) {
    Serial.print("Intentando connectr a MQTT...");
    if (mqtt_client.connect(ID_PLACA.c_str(), user.c_str(), pass.c_str(),willTopic.c_str(),willQoS,willRetain,willMessage.c_str())) {
      Serial.println(" connectdo a broker: " + server);
      mqtt_client.subscribe(topic_config.c_str());
      mqtt_client.subscribe(topic_brightness_get.c_str());
      mqtt_client.subscribe(topic_color_get.c_str());
      mqtt_client.subscribe(topic_switch_get.c_str());
      mqtt_client.subscribe(topic_fota.c_str());

      publish_mqtt_message(topic_connection, mqtt_connection_body(true), true);

    } else {
      Serial.println("ERROR:"+ String(mqtt_client.state()) +" reintento en 5s" );
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//sends mqtt message
void publish_mqtt_message(String topic, String body, bool isRetained = false){
  //TODO: CHECK IF "IF" AND RETAINED MSG WORKS
  if(isRetained)
  {
    mqtt_client.publish(topic.c_str(), body.c_str(), body.length(), isRetained);
    Serial.print("RETAINED Message sent to [");
  }
  else
  {
    mqtt_client.publish(topic.c_str(), body.c_str());
    Serial.print("Message sent to [");
  }
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(body);
}

//-----functions for setting up bodies of mqtt messages-----
String mqtt_connection_body(bool onlineArg) {
  String onlineString = onlineArg ? "true" : "false";

  StaticJsonDocument<300> body;
  body["CHIPID"] = CHIPID;
  body["online"] = onlineString;

  String buffer;
  serializeJson(body, buffer);
  return buffer;
}

String mqtt_data_body(unsigned long UptimeArg, float tempArg, float humArg) {  
  StaticJsonDocument<300> body;
  body["CHIPID"] = CHIPID;
  body["Uptime"] = UptimeArg;
  JsonObject dhtData = body.createNestedObject("DHT11");
  dhtData["temp"] = tempArg;
  dhtData["hum"] = humArg;
  JsonObject wifiData = body.createNestedObject("Wifi");
  wifiData["SSId"] = ssid;
  wifiData["IP"] = WiFi.localIP().toString();
  wifiData["RSSI"] = WiFi.RSSI();

  String buffer;
  serializeJson(body, buffer);
  return buffer;
}

//ADD FUNCTIONS FOR ALL PUBLISHED TOPICS
//-----------------------------------------------------

//process mqtt message
void process_msg(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received from [");
  Serial.print(topic);
  Serial.print("]: ");
  
  // Convertir el payload a un string
  char message[length + 1];
  strncpy(message, (char*)payload, length);
  message[length] = '\0'; 

  Serial.println(message);

  // Process JSON accordingly to the topic
  if (topic == topic_config) {
    process_mqtt_config(message);
  }
  else if (topic == topic_brightness_get) {
    process_mqtt_brightness(message);
  }
  else if (topic == topic_color_get) {
    process_mqtt_color(message);
  }
  else if (topic == topic_switch_get) {
    process_mqtt_switch(message);
  }
  else if (topic == topic_fota) {
    process_mqtt_fota();
  }
}

void bad_message(){
  Serial.print("[ERROR]: ");
  Serial.println("Some required in received message is null!");
}

//-----functions for processing mqtt messages-----
//ADD FUNCTIONS FOR ALL SUBSCRIBED TOPICS
void process_mqtt_config(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }

  //{"envia":300, "actualiza":60, "velocidad":10,"SWITCH":0}
  //if value is NOT NULL store it
  sendDataPeriod = !body["envia"].isNull() ? body["envia"] : sendDataPeriod;
  checkUpdatePeriod = !body["actualiza"].isNull() ? body["actualiza"] : checkUpdatePeriod;
  RGBChangeSpeed = !body["velocidad"].isNull() ? body["velocidad"] : RGBChangeSpeed;
  stateInterruptor = !body["SWITCH"].isNull() ? body["SWITCH"] : stateInterruptor;
  }
}

void process_mqtt_brightness(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }

  if (body["level"].isNull()){
    bad_message();
  }

  // { "level":75 } || { "level":75 , "id":"123456789"}
  LEDBrightness = body["level"];
  LEDBrightnessID = !body["id"].isNull() ? body["id"] : LEDBrightnessID;
  LEDBrightnessOrigin = "mqtt";
}

void process_mqtt_color(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }

  if (body["R"].isNull() || body["G"].isNull() || body["B"].isNull()){
    bad_message();
  }

  // { "R":255, "G":0, "B":0 } || { "R":255, "G":0, "B":0 , "id":"123456789"}
  r = body["R"];
  g = body["G"];
  b = body["B"];
  LEDColorID = !body["id"].isNull() ? body["id"] : LEDColorID;
}

void process_mqtt_switch(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }

  if (body["level"].isNull()){
    bad_message();
  }

  // { "level":0 } || { "level":1 , "id":"123456789"}
  LEDOn = body["level"];
  LEDOnID = !body["id"].isNull() ? body["id"] : LEDOnID;
}

void process_mqtt_fota(){
  intenta_OTA();
}
//-----------------------------------------------------
//-----------------------------------------------------





//-----------------------------------------------------
// LED MANAGING FUNCTION
//-----------------------------------------------------
void updateLED(unsigned long ahoraArg){

  if(!LEDOn) { //if LED should be OFF
    if(isLEDOn){ //if it's ON for real set it to OFF and return
      digitalWrite(RGB_BUILTIN,LOW);
      isLEDOn = false;
    }
    return;
  }
  else { //remember that it's already on and...
  isLEDOn = true;
  // update LED Brightness, then...
  // update current RGB colors according to the brightness and rgb values, then...
  // set the calculated new values to the LED
  
  int currR, currG, currB;

  if ((currLEDBrightness != aimLEDBrightness) && (ahoraArg - last_LEDBrightness_change >= LEDChangePeriod)){
    int difference = aimLEDBrightness-currLEDBrightness;
    currLEDBrightness += (difference > 0) - (difference < 0); //add +/- 1% brightness
  }
  else if ((r == currR) && (g == currG) && (b == currB)){
    return;
  }
  currR = r * currLEDBrightness/100;
  currG = g * currLEDBrightness/100;
  currB = b * currLEDBrightness/100;
  
  rgbLedWrite(RGB_BUILTIN, currR, currG, currB);
  }
}
//-----------------------------------------------------
//-----------------------------------------------------




//-----------------------------------------------------
// DHT DATA FUNCTION
//-----------------------------------------------------
void measureSendData(unsigned long ahoraArg){ //get measures and send the data
  if (ahora - last_data_msg >= sendDataPeriod * 1000) { //every 5 minutes
    last_data_msg = ahora;

    // get measures
    hum = dht.getHumidity();
    temp = dht.getTemperature();
    publish_mqtt_message(topic_connection,mqtt_data_body(ahora,temp,hum));
  }
}
//-----------------------------------------------------
//-----------------------------------------------------





void setup() { // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  Serial.println("Start setup...");

  // crea topics usando id de la placa
  // ChipID
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  ID_PLACA="ESP_" + String( chipId );
  connect_wifi();
  CHIPID= WiFi.getHostname();
  Serial.println("CHIPID: "+ CHIPID);

  //set up MQTT
  topic_connection = "II16/" + CHIPID +"/conexion";
  topic_data = "II16/" + CHIPID +"/datos";
  topic_config = "II16/" + CHIPID +"/config";
  topic_brightness_get = "II16/" + CHIPID +"/led/brillo";
  topic_brightness_set = "II16/" + CHIPID +"/led/brillo/estado";
  topic_color_get = "II16/" + CHIPID +"/led/color";
  topic_color_set = "II16/" + CHIPID +"/led/color/estado";
  topic_switch_get = "II16/" + CHIPID +"/switch/cmd";
  topic_switch_set = "II16/" + CHIPID +"/switch/status";
  topic_fota = "II16/" + CHIPID +"/FOTA";
  mqtt_client.setServer(server.c_str(), 1883);
  mqtt_client.setBufferSize(512); // para poder enviar messages de hasta X bytes
  mqtt_client.setCallback(process_msg);
  connect_mqtt();

  //set up OTA -- TODO/check LATER
  //intenta_OTA(); 

  //setup DHT sensor
  dht.setup(PIN_DHT, DHTesp::DHT11);

  Serial.println("Setup finished in " +  String(millis()) + " ms");
}      


void loop() { // put your main code here, to run repeatedly:
  if (!mqtt_client.connected()) {
    connect_mqtt();
  }
  mqtt_client.loop(); // esta llamada para que la librería recupere el control
  
  unsigned long ahora = millis();
  
  measureSendData(ahora); //measure and send DHT data periodically

  updateLED(); //update LED values if necessary
}
