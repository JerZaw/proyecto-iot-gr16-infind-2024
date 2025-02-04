#include <string>
#include <DHTesp.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPUpdate.h>
#include <HTTPClient.h>
#include "Button2.h"


#define WOKWI_SIM
#ifdef WOKWI_SIM
//-------------------PARTE NECESARIO PARA EL SIMULADOR------------------------
#include <Adafruit_NeoPixel.h>
// #define rgbLedWrite neopixelWrite // se sustituye el rgbledwrite por neopixelwrite ya que la version no es la mas reciente
Adafruit_NeoPixel LED_RGB(1, 8, NEO_GRBW + NEO_KHZ800);  // Creamos el objeto que manejará el led rgb

void setupSimulation(){
  LED_RGB.begin();            // Inicia el objeto que hemos creado asociado a la librería NeoPixel 
  LED_RGB.setBrightness(150); // Para el brillo del led
}

void simulateLED(int r, int g, int b){
  LED_RGB.setPixelColor(0, uint32_t(LED_RGB.Color(r,   g,   b))); // set color
  LED_RGB.show(); // Enciende el color
}
//-------------------PARTE NECESARIO PARA EL SIMULADOR------------------------
#endif







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
Button2 button;

//connections data
#ifndef WOKWI_SIM
const String ssid = "infind";
const String password = "1518wifi";
#endif

#ifdef WOKWI_SIM
// IF IN SIMULATION:
const String ssid = "Wokwi-GUEST";
const String password = "";
#endif

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
String topic_log;


//other CONST data
#define PIN_DHT 4
#define BUTTON_PIN  9
#define PIN_ON_OFF 5


//state variables
float hum;
float temp;
unsigned long uptime;

//VARIABLES FROM */config
int sendDataPeriod = 3*60; //period of sending */datos in seconds
int checkUpdatePeriod = 0; //period of checking OTA in minutes -- if=0, then no periodic check
int LEDChangePeriod = 10; //period of changing ±1% in LED Brightness, in miliseconds
bool ControllablePin = true; //LED state interruptor - onn/off (GPIO5)

//VARIABLES FROM */switch/cmd
int LEDOnID;
bool LEDOn = true;

//VARIABLES FROM */led/brillo
int aimLEDBrightness = 10; //wanted LED brightness, in range 0-100
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
bool LEDBrightnessArrived = 1; // if the current LED Brightness already arrived to the aim value

//PERODIC FUNCTIONS VARIABLES
unsigned long last_data_msg = 0;
unsigned long last_LEDBrightness_change = 0;
unsigned long last_FOTA_period = 0;





//-----------------------------------------------------
// funciones para progreso de OTA
//-----------------------------------------------------
void inicio_OTA(){ serial_logln(DEBUG_STRING+"Nuevo Firmware encontrado. Actualizando..."); }
void final_OTA() { serial_logln(DEBUG_STRING+"Fin OTA. Reiniciando..."); }
void error_OTA(int e){ serial_logln(DEBUG_STRING+"ERROR OTA: "+String(e)+" "+httpUpdate.getLastErrorString()); }
void progreso_OTA(int x, int todo){
  int progreso=(int)((x*100)/todo);
  String espacio = (progreso<10)? "  ":(progreso<100)? " " : "";
  if(progreso%10==0) serial_logln(DEBUG_STRING+"Progreso: "+espacio+String(progreso)+"% - "+String(x/1024)+"K de "+String(todo/1024)+"K");
}
//-----------------------------------------------------
void intenta_OTA(){ 
  serial_logln( "---------------------------------------------" );  
  serial_logln( DEBUG_STRING+"MAC de la placa: " + WiFi.macAddress());
  serial_logln( DEBUG_STRING+"Comprobando actualización:" );
  serial_logln( DEBUG_STRING+"URL: "+OTA_URL );
  serial_logln( "---------------------------------------------" );  
  httpUpdate.setLedPin(RGB_BUILTIN);
  httpUpdate.onStart(inicio_OTA);
  httpUpdate.onError(error_OTA);
  httpUpdate.onProgress(progreso_OTA);
  httpUpdate.onEnd(final_OTA);

  WiFiClient wClient;

  switch(httpUpdate.update(wClient, OTA_URL, HTTP_OTA_VERSION)) {
    case HTTP_UPDATE_FAILED:
      serial_logln(DEBUG_STRING+"HTTP update failed: Error ("+String(httpUpdate.getLastError())+"): "+httpUpdate.getLastErrorString());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      serial_logln(DEBUG_STRING+"El dispositivo ya está actualizado");
      break;
    case HTTP_UPDATE_OK:
      serial_logln(DEBUG_STRING+"OK");
      break;
    }
}
//-----------------------------------------------------
//-----------------------------------------------------


//LOG AND SERIAL
void serial_logln(String message){
  Serial.println(message);
  mqtt_client.publish(topic_log.c_str(), message.c_str());
}


//WIFI connection
void connect_wifi() {
  serial_logln("Connecting to " + ssid);
 
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    serial_logln(".");
  }
  serial_logln("WiFi connected, IP address: " + WiFi.localIP().toString());
}


//-----------------------------------------------------
//MQTT
//-----------------------------------------------------
//sends mqtt message
void publish_mqtt_message(String topic, String body, bool isRetained = false){
  //TODO: CHECK IF "IF" AND RETAINED MSG WORKS
  String messageStart;
  if(isRetained)
  {
    char buffer[256];
    body.toCharArray(buffer, 256);

    mqtt_client.publish(topic.c_str(), (const uint8_t*)buffer , body.length(), isRetained);
    messageStart = "RETAINED Message sent to [";
  }
  else
  {
    mqtt_client.publish(topic.c_str(), body.c_str());
    messageStart = "Message sent to [";
  }
  serial_logln(messageStart + topic + "]: " + body);
}

//MQTT connection
void connect_mqtt() {
  // Loop until we're reconnected
  String willTopic = topic_connection;  // El tópico de LWT (conexion_topic)
  String willMessage = "{\"CHIPID\" : \"" + CHIPID + "\", \"online\" : false}";  // message de LWT para indicar desconexión
  int willQoS = 1;  // QoS del message de LWT (nivel de calidad)
  bool willRetain = true; 
  while (!mqtt_client.connected()) {
    serial_logln("Intentando conectar a MQTT...");
    if (mqtt_client.connect(ID_PLACA.c_str(), user.c_str(), pass.c_str(),willTopic.c_str(),willQoS,willRetain,willMessage.c_str())) {
      serial_logln(" conectando a broker: " + server);
      mqtt_client.subscribe(topic_config.c_str());
      mqtt_client.subscribe(topic_brightness_get.c_str());
      mqtt_client.subscribe(topic_color_get.c_str());
      mqtt_client.subscribe(topic_switch_get.c_str());
      mqtt_client.subscribe(topic_fota.c_str());

      publish_mqtt_message(topic_connection, mqtt_connection_body(true), true);

    } else {
      serial_logln("ERROR:"+ String(mqtt_client.state()) +" reintento en 5s" );
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
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

String mqtt_brightnessState_body() {  
  StaticJsonDocument<300> body;
  body["CHIPID"] = CHIPID;
  body["LED"] = aimLEDBrightness;
  body["origen"] = LEDBrightnessOrigin;

  if(LEDBrightnessID != 0){
    body["id"] = LEDBrightnessID;
  }

  String buffer;
  serializeJson(body, buffer);
  return buffer;
}

String mqtt_colorState_body() {  
  StaticJsonDocument<300> body;
  body["CHIPID"] = CHIPID;
  body["R"] = r;
  body["G"] = g;
  body["B"] = b;

  if(LEDColorID != 0){
    body["id"] = LEDColorID;
  }

  String buffer;
  serializeJson(body, buffer);
  return buffer;
}

String mqtt_switch_body() {  
  StaticJsonDocument<300> body;
  body["CHIPID"] = CHIPID;

  int level;
  level = (LEDOn) ? 1 : 0;
  body["SWITCH"] = level;

  if(LEDOnID != 0){
    body["id"] = LEDOnID;
  }

  String buffer;
  serializeJson(body, buffer);
  return buffer;
}

//-----------------------------------------------------
//-----functions for processing mqtt messages-----
void process_mqtt_config(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    serial_logln("Error al deserializar el JSON");
    return;
  }

  //{"envia":300, "actualiza":60, "velocidad":10,"SWITCH":0}
  //if value is NOT NULL store it
  sendDataPeriod = !body["envia"].isNull() ? body["envia"] : sendDataPeriod;
  checkUpdatePeriod = !body["actualiza"].isNull() ? body["actualiza"] : checkUpdatePeriod;
  LEDChangePeriod = !body["velocidad"].isNull() ? body["velocidad"] : LEDChangePeriod;
  ControllablePin = !body["SWITCH"].isNull() ? body["SWITCH"] : ControllablePin;

  if(ControllablePin)
  {
    digitalWrite(PIN_ON_OFF, HIGH);
  }
  else {
    digitalWrite(PIN_ON_OFF, LOW);
  }  
}

void process_mqtt_brightness(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    serial_logln("Error al deserializar el JSON");
    return;
  }

  if (body["level"].isNull()){
    bad_message();
  }

  // { "level":75 } || { "level":75 , "id":"123456789"}
  aimLEDBrightness = body["level"];
  LEDBrightnessID = !body["id"].isNull() ? body["id"] : 0;
  //LEDBrightnessOrigin = "mqtt";
  LEDBrightnessOrigin = !body["origin"].isNull() ? body["origin"] : LEDBrightnessOrigin;
}

void process_mqtt_color(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    serial_logln("Error al deserializar el JSON");
    return;
  }

  if (body["R"].isNull() || body["G"].isNull() || body["B"].isNull()){
    bad_message();
  }

  // { "R":255, "G":0, "B":0 } || { "R":255, "G":0, "B":0 , "id":"123456789"}
  r = body["R"];
  g = body["G"];
  b = body["B"];
  LEDColorID = !body["id"].isNull() ? body["id"] : 0;

  publish_mqtt_message(topic_color_set, mqtt_colorState_body());
}

void process_mqtt_switch(char* message){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, message);
  if (error) {
    serial_logln("Error al deserializar el JSON");
    return;
  }

  if (body["level"].isNull()){
    bad_message();
  }

  // { "level":0 } || { "level":1 , "id":"123456789"}
  LEDOn = body["level"];
  LEDOnID = !body["id"].isNull() ? body["id"] : 0;

  publish_mqtt_message(topic_switch_set, mqtt_switch_body());
  
}

void process_mqtt_fota(){
  intenta_OTA();
}


//process mqtt message
void process_msg(char* topic, byte* payload, unsigned int length) {  
  // Convertir el payload a un string
  char message[length + 1];
  strncpy(message, (char*)payload, length);
  message[length] = '\0'; 

  String newTopic = topic;
  //need to store the topic separately
  //because the client uses the same buffer for both inbound and outbound messages
  //SEE: https://pubsubclient.knolleary.net/api#callback

  serial_logln(String("Message received from [") + String(topic) + "]: " + String(message));

  // Process JSON accordingly to the topic
  if (newTopic == topic_config) {
    process_mqtt_config(message);
  }
  else if (newTopic == topic_brightness_get) {
    process_mqtt_brightness(message);
  }
  else if (newTopic == topic_color_get) {
    process_mqtt_color(message);
  }
  else if (newTopic == topic_switch_get) {
    process_mqtt_switch(message);
  }
  else if (newTopic == topic_fota) {
    process_mqtt_fota();
  }
}

void bad_message(){
  serial_logln(String("[ERROR]: ") + "Some required data in received message is null!");
}

//-----------------------------------------------------
//-----------------------------------------------------




//-----------------------------------------------------
// LED MANAGING FUNCTION
//-----------------------------------------------------
void updateLED(unsigned long ahoraArg) {
  if (!LEDOn) { // LED should be OFF
    if (isLEDOn) { // If it’s currently ON, turn it OFF
      digitalWrite(RGB_BUILTIN, LOW);
      isLEDOn = false;
    }
    return;
  }
  
  // If LED is supposed to be ON
  isLEDOn = true;

  // Calculate current RGB values based on brightness
  int currR = r * currLEDBrightness / 100;
  int currG = g * currLEDBrightness / 100;
  int currB = b * currLEDBrightness / 100;

  // Check if brightness needs to change
  if ((currLEDBrightness != aimLEDBrightness) && 
      (ahoraArg - last_LEDBrightness_change >= LEDChangePeriod)) {
      
    // Adjust brightness toward target (±1%)
    int difference = aimLEDBrightness - currLEDBrightness;
    currLEDBrightness += (difference > 0) - (difference < 0); // Increment or decrement by 1%

    // Update the time of the last brightness change
    last_LEDBrightness_change = ahoraArg;
  }

  // Update LED if brightness or color changes
  // IF IN REAL ESP32C3-DEV BOARD: rgbLedWrite(RGB_BUILTIN, currR, currG, currB);
  // IF IN WOKWI SIMULATION:       simulateLED(currR, currG, currB);
  #ifdef WOKWI_SIM
  simulateLED(currR, currG, currB);
  #endif

  #ifndef WOKWI_SIM
  rgbLedWrite(RGB_BUILTIN, currR, currG, currB);
  #endif



  // If brightness reaches the target and color is correct, send MQTT message
  if ((currLEDBrightness == aimLEDBrightness) && 
      (currR == r * aimLEDBrightness / 100) && 
      (currG == g * aimLEDBrightness / 100) && 
      (currB == b * aimLEDBrightness / 100)) {

    if (!LEDBrightnessArrived) { // Notify once
      publish_mqtt_message(topic_brightness_set, mqtt_brightnessState_body());
      LEDBrightnessArrived = true;
    }
  } else {
    LEDBrightnessArrived = false; // Reset flag if not yet arrived
  }
}

//-----------------------------------------------------
//-----------------------------------------------------



//-----------------------------------------------------
// DHT DATA FUNCTION
//-----------------------------------------------------
void measureSendData(unsigned long ahoraArg){ //get measures and send the data
  if (ahoraArg - last_data_msg >= sendDataPeriod * 1000) { //every 5 minutes
    last_data_msg = ahoraArg;

    // get measures
    hum = dht.getHumidity();
    temp = dht.getTemperature();
    publish_mqtt_message(topic_data,mqtt_data_body(ahoraArg,temp,hum));
  }
}
//-----------------------------------------------------
//-----------------------------------------------------



//-----------------------------------------------------
// FOTA UPDATE PERIOD
//-----------------------------------------------------
void FOTAperiod(unsigned long ahoraArg){ //get measures and send the data
  if ((ahoraArg - last_FOTA_period >= checkUpdatePeriod * 60 * 1000) && (checkUpdatePeriod != 0)) { //every X minutes
    last_FOTA_period = ahoraArg;
    intenta_OTA();
  }
}
//-----------------------------------------------------
//-----------------------------------------------------




//-----------------------------------------------------
// BUTTON MANAGING
//-----------------------------------------------------
void singleClick(Button2& btn) {
  if (LEDOn==false){
    digitalWrite(RGB_BUILTIN, HIGH);
    LEDOn=true;
  }
  else {
    digitalWrite(RGB_BUILTIN, LOW);
    LEDOn=false;
  }
  publish_mqtt_message(topic_switch_set, mqtt_switch_body());
  serial_logln("click\n");
}
void longClick(Button2& btn) {
    intenta_OTA();

    serial_logln("long click\n");
}
void doubleClick(Button2& btn) {
    aimLEDBrightness = 100;
    LEDBrightnessOrigin = "Pulsador";

    serial_logln("double click\n");
}
//-----------------------------------------------------
//-----------------------------------------------------


void setupWiFi(){
  // crea topics usando id de la placa
  // ChipID
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) {
    chipId = ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  ID_PLACA="ESP_" + String( chipId );
  connect_wifi();
  CHIPID= WiFi.getHostname();
  serial_logln("CHIPID: "+ CHIPID);
}

void setupMqtt(){
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
  topic_log = "II16/" + CHIPID +"/log";
  mqtt_client.setServer(server.c_str(), 1883);
  mqtt_client.setBufferSize(512); // para poder enviar messages de hasta X bytes
  mqtt_client.setCallback(process_msg);
  connect_mqtt();
}

void setupButton(){
  button.begin(BUTTON_PIN);
  button.setClickHandler(singleClick);
  button.setLongClickHandler(longClick);
  button.setDoubleClickHandler(doubleClick);
  //button.setTripleClickHandler(tripleClick);
  pinMode(RGB_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN , INPUT_PULLUP);
}

void setupControllablePin(){
  pinMode(PIN_ON_OFF, OUTPUT);
}


void setup() { // put your setup code here, to run once:
  Serial.begin(115200);
  serial_logln("");
  serial_logln("Start setup...");

  //set up WiFi connection and de
  setupWiFi();

  //set up MQTT
  setupMqtt();

  //set up OTA -- TODO/check LATER
  intenta_OTA(); 

  //setup DHT sensor
  dht.setup(PIN_DHT, DHTesp::DHT11);

  //setup BUTTON MANAGEMENT
  setupButton();

  setupControllablePin();

  serial_logln("Setup finished in " +  String(millis()) + " ms");
}      


void loop() { // put your main code here, to run repeatedly:
  if (!mqtt_client.connected()) {
    connect_mqtt();
  }
  mqtt_client.loop(); // esta llamada para que la librería recupere el control
  
  button.loop();

  unsigned long ahora = millis();
  
  measureSendData(ahora); //measure and send DHT data periodically

  updateLED(ahora); //update LED values if necessary

  FOTAperiod(ahora);//update FOTA period
}