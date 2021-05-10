#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <FastLED.h>

const char* ssid = "";
const char* password = "";

// Globals for power indicator /////////////////////
int ON = D0; // ESP8266 Pin to which onboard LED is connected
unsigned long previousMillis = 0;  // will store last time LED was updated
const long interval = 1000;  // interval at which to blink (milliseconds)
int ledState = LOW;  // ledState used to set the LED
////////////////////////////////////////////////////

#define N_PIXELS 148
#define MAX_MA 2000
#define COLOR_ORDER GRB
#define LED_TYPE WS2812B
#define D_P1 6
#define D_P2 7

ESP8266WebServer server(80); // global for server on port 80
CRGB ledsL[N_PIXELS];
CRGB ledsR[N_PIXELS];
bool ledStatus = LOW;

void setup() {
  ///////////// DONT TOUCH //////////////////////////////////
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("ESP8266");

  // No authentication by default
  ArduinoOTA.setPassword("ChangeMe");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
  pinMode(ON, OUTPUT);
  ArduinoOTA.onStart([]() {
  
    
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  server.on("/", onConnect);
  server.on("/Power", onPower);
  server.on("/SetColor", onSetColor);
  server.onNotFound(notFound);

  server.begin();
  Serial.println("HTTP server started");
  ///////////// DONT TOUCH //////////////////////////////////
  FastLED.addLeds<LED_TYPE, D_P1>(ledsL, N_PIXELS);
  FastLED.addLeds<LED_TYPE, D_P2>(ledsR, N_PIXELS);
  
}

void loop() {
  ArduinoOTA.handle();
  /////////////////////// Lets us know the system is powered on! ////////////////////////
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
  // save the last time you blinked the LED
  previousMillis = currentMillis;
  // if the LED is off turn it on and vice-versa:
  ledState = not(ledState);
  // set the LED with the ledState of the variable:
  digitalWrite(ON,  ledState);
  ///////////////////////////////////////////////////////////////////////////////////////
  // put your main code here, to run repeatedly:
  }
  server.handleClient();
  
}

void onConnect() {
  // What do we want to happen upon a user connecting to the page
  // Set the status to low, send the webpage
  // is there any defaults for the vu meter we need to initialize?
  Serial.println("User has connected: ");
  ledStatus = LOW;
  server.send(200, "text/html", SendHTML(ledStatus));
}

void onPower() {
  // When a user clicks the power button do the following
  // If the strip is on, turn it off, if it's off turn it on
  // update the status indicator above the power button

  if(ledsL[0]){ // there is some sort of light and we want them off
    Serial.println("VUME turning off");
    ledStatus = LOW;
    FastLED.clear();
  }
  else{ // they are off already and we want to turn on
    Serial.println("VUME turning on");
    ledStatus = HIGH;
    for(int i = 0; i < N_PIXELS; i++){
      ledsL[i] = CRGB::White;
      ledsR[i] = CRGB::White;
    }
  }
  FastLED.show();
  server.send(200, "text/html", SendHTML(ledStatus));
}

void onSetColor() {
  Serial.println("Setting color to: ");
  Serial.println(server.args());
  server.send(200, "text/html", SendHTML(ledStatus));
}

void notFound (){
  // Webpage was not found
  server.send(404, "text/plain", "Not found");
}

String SendHTML(uint8_t led_status){
  String page = "<!doctype html>";
  page += "<html lang=\"en\" class=\"h-100\">";
  page += "<head>";
  page += "<meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  page += "<meta name=\"Agent\" content=\"SecretAgentMan\">";
  page += "<title>VUME: Stero VU Meter</title>";
  page += "<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\" integrity=\"sha384-ggOyR0iXCbMQv3Xipma34MD+dH/1fQ784/j6cY/iJTQUOhcWr7x9JvoRxT2MZw1T\" crossorigin=\"anonymous\">";
  page += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/jscolor/2.0.4/jscolor.min.js\"></script>";
  page += "</head>";
  page += "<body class=\"container d-flex h-100 text-center text-white bg-dark\">";
  page += "<div class=\"container d-flex w-25 h-auto p-3 mx-auto flex-column\">";
  page += "<header class=\"mb-5\">";
  page += "<div class=\"h-50 mb-5\">";
  page += "<h3 class=\"float-md-start mb-0\">VUME</h3></div></header>";
  page += "<h1 class=\"mt-5\">VUME</h1>";
  page += "<p>Led Status: OFF</p>";
  page += "<a href=\"/Power\" class=\"h6 h-10 btn btn lg btn-secondary fw-bold border-white bg-white text-dark\">ON/OFF</a>";
  page += "<a href=\"/SetColor\" class=\"h6 h-10 btn btn lg btn-secondary fw-bold border-white bg-white text-dark\" id=\"change_color\" role=\"button\">Set Color</a>";
  page += "<input class=\"h6 jscolor {onFineChange:'update(this)'}\" id=\"rgb\">";
  page += "<script>function update(picker) {document.getElementById('rgb').innerHTML = Math.round(picker.rgb[0]) + ', ' +  Math.round(picker.rgb[1]) + ', ' + Math.round(picker.rgb[2])";
  page += "document.getElementById(\"change_color\").href=\"?r\" + Math.round(picker.rgb[0]) + \"g\" +  Math.round(picker.rgb[1]) + \"b\" + Math.round(picker.rgb[2]) + \"&\";}</script>";
  page += "<a href=\"#\" class=\"h6 btn btn-lg btn-secondary fw-bold border-white bg-white dropdown-toggle text-dark\" id=\"dropdownMenuButton\" data-toggle=\"dropdown\" aria-haspopup=\"true\" aria-expanded=\"false\">Mode Select</a>";
  page += "<div class=\"h6 h-10 dropdown-menu\" aria-labelledby=\"dropdownMenuButton\">";
  page += "<a href=\"Mode/Solid\" class=\"h6 dropdown-item\">Solid</a>";
  page += "<a href=\"Mode/VUMeter\" class=\"h6 dropdown-item\">VU Meter</a></div></div>";
  page += "<script src=\"https://code.jquery.com/jquery-3.3.1.slim.min.js\" integrity=\"sha384-q8i/X+965DzO0rT7abK41JStQIAqVgRVzpbzo5smXKp4YfRvH+8abtTE1Pi6jizo\" crossorigin=\"anonymous\"></script>";
  page += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\" integrity=\"sha384-UO2eT0CpHqdSJQ6hJty5KVphtPhzWj9WO1clHTMGa3JDZwrnQq4sF86dIHNDz0W1\" crossorigin=\"anonymous\"></script>";
  page += "<script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\" integrity=\"sha384-JjSmVgyd0p3pXB1rRibZUAYoIIy6OrQ6VrjIEaFf/nJGzIxFDsf4x0xIM+B07jRM\" crossorigin=\"anonymous\"></script></body></hmml>";
  return page;
}
