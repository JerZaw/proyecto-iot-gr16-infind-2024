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

// definimos macro para indicar función y línea de código en los mensajes
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
String topic_conexion;
String topic_datos;
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
bool online;
float hum;
float temp;
unsigned long uptime;

int sendMessagesPeriod = 5*60; //in seconds
int checkUpdatePeriod; //in seconds
int RGBChangeSpeed; //miliseconds while chaning a ±1% in RGB
bool stateInterruptor; //se comportará como interruptor de dos estados encendido/apagado (GPIO5).

// unsigned long ultimo_mensaje=0;    
// int led_status;

// int red = 255;
// int green = 0;
// int blue = 0;
// int estado_led=0;



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
void conecta_wifi() {
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

//MQTT connection
void conecta_mqtt() {
  // Loop until we're reconnected
  String willTopicS = topic_conexion;  // El tópico de LWT (conexion_topic)
  String willMessageS = "{\"CHIPID\" : \"esp32c3-" + CHIPID + "\", \"online\" : false}";  // Mensaje de LWT para indicar desconexión
  const char* willTopic = willTopicS.c_str();
  const char* willMessage = willMessageS.c_str();

  int willQoS = 1;  // QoS del mensaje de LWT (nivel de calidad)
  bool willRetain = true; 
  while (!mqtt_client.connected()) {
    Serial.print("Intentando conectar a MQTT...");
    if (mqtt_client.connect(ID_PLACA.c_str(), user.c_str(), pass.c_str(),willTopic,willQoS,willRetain,willMessage)) {
      Serial.println(" conectado a broker: " + server);
      mqtt_client.subscribe(topic_config.c_str());
      mqtt_client.subscribe(topic_brightness_get.c_str());
      mqtt_client.subscribe(topic_color_get.c_str());
      mqtt_client.subscribe(topic_switch_get.c_str());
      mqtt_client.subscribe(topic_fota.c_str());

      publish_mqtt_message(topic_conexion, mqtt_connection_body(true));

    } else {
      Serial.println("ERROR:"+ String(mqtt_client.state()) +" reintento en 5s" );
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//sends mqtt message
void publish_mqtt_message(String topic, String body){
  mqtt_client.publish(topic.c_str(), body.c_str());

  Serial.print("Message sent to [");
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
  body["CHIPID"] = "esp32c3-" + CHIPID;
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
void procesa_mensaje(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received from [");
  Serial.print(topic);
  Serial.print("]: ");
  
  // Convertir el payload a un string
  char mensaje[length + 1];
  strncpy(mensaje, (char*)payload, length);
  mensaje[length] = '\0'; 

  Serial.println(mensaje);

  // Process JSON accordingly to the topic
  if (topic == topic_config) {
    process_mqtt_config(mensaje);
  }
  else if (topic == topic_brightness_get) {
    process_mqtt_brightness(mensaje);
  }
  else if (topic == topic_color_get) {
    process_mqtt_color(mensaje);
  }
  else if (topic == topic_switch_get) {
    process_mqtt_switch(mensaje);
  }
  else if (topic == topic_fota) {
    process_mqtt_fota();
  }
}

//-----functions for processing mqtt messages-----
//ADD FUNCTIONS FOR ALL SUBSCRIBED TOPICS
void process_mqtt_config(char* mensaje){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, mensaje);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }

  //{"envia":300, "actualiza":60, "velocidad":10,"SWITCH":0}
  if (body["envia"].isNull() || body["actualiza"].isNull() || body["velocidad"].isNull() || body["SWITCH"].isNull()){
    Serial.println("Something is null");
  }
  else {
    //every data is correct -- process it
    Serial.println("All correct");

    //if value is NOT NULL store it
    sendMessagesPeriod = !body["envia"].isNull() ? body["envia"] : sendMessagesPeriod;
    checkUpdatePeriod = !body["actualiza"].isNull() ? body["actualiza"] : checkUpdatePeriod;
    RGBChangeSpeed = !body["velocidad"].isNull() ? body["velocidad"] : RGBChangeSpeed;
    stateInterruptor = !body["SWITCH"].isNull() ? body["SWITCH"] : stateInterruptor;
  }
}

void process_mqtt_brightness(char* mensaje){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, mensaje);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }


}

void process_mqtt_color(char* mensaje){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, mensaje);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }


}

void process_mqtt_switch(char* mensaje){
    // Parsear el JSON recibido
  StaticJsonDocument<200> body;
  DeserializationError error = deserializeJson(body, mensaje);
  if (error) {
    Serial.println("Error al deserializar el JSON");
    return;
  }


}

void process_mqtt_fota(){
  intenta_OTA();
}
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
  conecta_wifi();
  CHIPID= WiFi.getHostname();
  Serial.println("CHIPID: "+ CHIPID);

  //set up MQTT
  topic_conexion = "II16/" + CHIPID +"/conexion";
  topic_datos = "II16/" + CHIPID +"/datos";
  topic_config = "II16/" + CHIPID +"/config";
  topic_brightness_get = "II16/" + CHIPID +"/led/brillo";
  topic_brightness_set = "II16/" + CHIPID +"/led/brillo/estado";
  topic_color_get = "II16/" + CHIPID +"/led/color";
  topic_color_set = "II16/" + CHIPID +"/led/color/estado";
  topic_switch_get = "II16/" + CHIPID +"/switch/cmd";
  topic_switch_set = "II16/" + CHIPID +"/switch/status";
  topic_fota = "II16/" + CHIPID +"/FOTA";
  mqtt_client.setServer(server.c_str(), 1883);
  mqtt_client.setBufferSize(512); // para poder enviar mensajes de hasta X bytes
  mqtt_client.setCallback(procesa_mensaje);
  conecta_mqtt();

  //set up OTA -- TODO/check LATER
  //intenta_OTA(); 

  //setup DHT sensor
  dht.setup(PIN_DHT, DHTesp::DHT11);

  Serial.println("Setup finished in " +  String(millis()) + " ms");
}      




void loop() { // put your main code here, to run repeatedly:
  if (!mqtt_client.connected()) {
    conecta_mqtt();
  }
  mqtt_client.loop(); // esta llamada para que la librería recupere el control
  
  // unsigned long ahora = millis();
  
  // if (ahora - ultimo_mensaje >= 30000) {
  //   ultimo_mensaje = ahora; 
  //   Serial.println();
  //   Serial.println("Topic   : "+ topic_datos);

    
  //   float hum = dht.getHumidity();
  //   float temp = dht.getTemperature();
  // }
}
