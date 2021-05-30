#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <FastLED.h>

const char* ssid = "";
const char* password = "";

// Globals for power indicator /////////////////////
#define ON D0 // ESP8266 Pin to which onboard LED is connected
unsigned long previousMillis = 0;  // will store last time LED was updated
const int interval = 1000;  // interval at which to blink (milliseconds)
bool ledState = LOW;  // ledState used to set the LED
////////////////////////////////////////////////////

#define N_PIXELS 148               // Number of LEDS
#define MAX_MA 2000                // Max amps pulled
#define COLOR_ORDER GRB            // Color order for the strip
#define LED_TYPE WS2812B           // LED type
#define D_P1 D2                    // Data out for Left
#define D_P2 D1                    // Data out for Right
#define DC_OFFSET 0                // Offset for dirty aux signal
#define NOISE 20                   // Noise value to filter out
#define SAMPLES 60                 // Length of buffer for dynamic adjustments
#define TOP (N_PIXELS + 2)         // Top bar max height goes off sccreen a little
#define PEAK_FALL 20               // Fall speed for top bar
#define N_PIXELS_HALF (N_PIXELS/2)
#define ANALOG A0                  // Analog pin for our reaings. We will have to multiplex it with nodemcu
int LEFT_SIGNAL = D6;             // Set this pin to high to set multiplexer to LEFT, and read LEFT signal
int RIGHT_SIGNAL = D7;            // Set this pin to high to set multiplexer to RIGHT, and read RIGHT signal

uint8_t volCountLeft = 0;           // Frame counter for storing past volume data
int volLeft[SAMPLES];               // Collection of prior volume samples
int lvlLeft = 0;                    // Current "dampened" audio level
int minLvlAvgLeft = 0;              // For dynamic adjustment of graph low & high
int maxLvlAvgLeft = 512;

uint8_t volCountRight = 0;          // Frame counter for storing past volume data
int volRight[SAMPLES];              // Collection of prior volume samples
int lvlRight = 0;                   // Current "dampened" audio level
int minLvlAvgRight = 0;             // For dynamic adjustment of graph low & high
int maxLvlAvgRight = 512;

uint8_t peakLeft, peakRight;
static uint8_t dotCountLeft, dotCountRight;

ESP8266WebServer server(80);       // global for server on port 80
CRGB ledsL[N_PIXELS];              // Left LED strip
CRGB ledsR[N_PIXELS];              // Right LED strip

void setup() {
  ///////////// DONT TOUCH BELOW //////////////////////////////////
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);                                   // Sets wifi mode to Station instead of access point
  WiFi.begin(ssid, password);                            // Initiate wifi connection with credentials
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {  // Keep trying to connect until status is WL_CONNECTED
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  pinMode(ON, OUTPUT);                                    // Sets the mode of the power LED to OUTPUT
  ArduinoOTA.setHostname("ESP8266");                      // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setPassword("ChangeMe");                     // No authentication by default
  
  // Arduino OTA on start function
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    Serial.println("Start updating " + type);
  });

  // Arduino OTA on end function
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  // Arduino OTA on progress function
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  // Arduino OTA on error function
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
  Serial.println("Wifi Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  server.on("/", onConnect);                                // Binds onConnect handler to main URL
  server.on("/Power", onPower);                             // Binds onPower handler to Power URL
  server.on("/SetColor", onSetColor);                       // Binds onSetColor handler to set URL
  server.onNotFound(notFound);                              // Binds notFound handler to 404 error

  server.begin();
  Serial.println("HTTP server started");
  ///////////// DONT TOUCH ABOVE //////////////////////////////////
  
  FastLED.addLeds<LED_TYPE, D_P1>(ledsL, N_PIXELS);         // Adds left strip to our FastLED workspace
  FastLED.addLeds<LED_TYPE, D_P2>(ledsR, N_PIXELS);         // Adds right strip to our FastLED workspace

  pinMode(ANALOG, INPUT);
  
}

void loop() {
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
  }
  ArduinoOTA.handle();                                      // Handles all OTA calls and fucntions
  server.handleClient();                                    // Handles all requests made from client

  // put your main code here, to run repeatedly:
  vu5(0);
  vu5(1);
}

// When a user connects, display connect message and serve webpage
void onConnect() {
  Serial.println("User has connected: ");
  server.send(200, "text/html", SendHTML());
}

// When a user clicks the power button, get the 
// current status and either turn the strip on or off
void onPower() {
  if(ledsL[0]){                                             // there is some sort of light and we want them off
    Serial.println("VUME turning off");                     // Print turning off message
    FastLED.clear();                                        // Clears data channel for FastLED
  }
  else{                                                     // they are off already and we want to turn on
    Serial.println("VUME turning on");                      // Print turning on message
    for(uint8_t i = 0; i < N_PIXELS; i++){                      // Loops through and sets all the LEDs for left and right to white
      ledsL[i] = CRGB::White;                               // Left = White
      ledsR[i] = CRGB::White;                               // Right = White
    }
  }
  FastLED.show();                                           // Push data channel updates to show
  server.send(200, "text/html", SendHTML());                // Sends the client the new webpage, not sure if I need to keep this with the Jquery
}

// Receives a HTTP post message containing the 
// hex value of the color requested by the user
// Converts it to CRGB and assigned to LEDS
void onSetColor() {
  Serial.println("Setting color to: ");
  String c = server.arg(1);                                 // Get the hex argument from the post message
  String color = "0x" + c.substring(1);                     // Remove the "#" from c, and add "0x" to the head
  Serial.println(color);
  for(uint8_t i=0; i<N_PIXELS; i++){                            // Loops through and assigns the color to each LED
    ledsL[i] = strtol(color.c_str(), NULL, 16);             // Turns color into direct Null terminated C-String, the casts it to int
    ledsR[i] = strtol(color.c_str(), NULL, 16);             // Turns color into direct Null terminated C-String, the casts it to int
  }
  server.send(200, "text/html", SendHTML());                // Sends the client the new webpage, not sure if I need to keep this with the JQuery
}

// Wrong URL, tell client "Error 404"
void notFound (){
  // Webpage was not found
  server.send(404, "text/plain", "Not found");              // Serve 404 message
}

uint16_t auxReading(uint8_t channel) {

  Serial.print("Reading Channel: ");
  Serial.println(channel);
  int n = 0;
  uint16_t height = 0;

  if(channel == 0) {
    pinMode(LEFT_SIGNAL, OUTPUT);
    pinMode(RIGHT_SIGNAL, INPUT);
    digitalWrite(LEFT_SIGNAL, HIGH);
    delay(100);
    int n = analogRead(ANALOG); // Raw reading from left line in
    digitalWrite(LEFT_SIGNAL, LOW);
    Serial.println(n);
    n = abs(n - 512 - DC_OFFSET); // Center on zero
    Serial.println(n);
    n = (n <= NOISE) ? 0 : (n - NOISE); // Remove noise/hum
    Serial.println(n);
    lvlLeft = ((lvlLeft * 7) + n) >> 3; // "Dampened" reading else looks twitchy (>>3 is divide by 8)
    volLeft[volCountLeft] = n; // Save sample for dynamic leveling
    volCountLeft = ++volCountLeft % SAMPLES;
    // Calculate bar height based on dynamic min/max levels (fixed point):
    height = TOP * (lvlLeft - minLvlAvgLeft) / (long)(maxLvlAvgLeft - minLvlAvgLeft);
    Serial.println(lvlLeft);
  }
  
  else {
    pinMode(RIGHT_SIGNAL, OUTPUT);
    pinMode(LEFT_SIGNAL, INPUT);
    digitalWrite(RIGHT_SIGNAL, HIGH);
    delay(100);
    int n = analogRead(ANALOG); // Raw reading from mic
    digitalWrite(RIGHT_SIGNAL, LOW);
    Serial.println(n);
    n = abs(n - 512 - DC_OFFSET); // Center on zero
    Serial.println(n);
    n = (n <= NOISE) ? 0 : (n - NOISE); // Remove noise/hum
    Serial.println(n);
    lvlRight = ((lvlRight * 7) + n) >> 3; // "Dampened" reading (else looks twitchy)
    volRight[volCountRight] = n; // Save sample for dynamic leveling
    volCountRight = ++volCountRight % SAMPLES;
    // Calculate bar height based on dynamic min/max levels (fixed point):
    height = TOP * (lvlRight - minLvlAvgRight) / (long)(maxLvlAvgRight - minLvlAvgRight);
    Serial.println(lvlRight);
  }

  // Calculate bar height based on dynamic min/max levels (fixed point):
  height = constrain(height, 0, TOP);
  return height;
}

void averageReadings(uint8_t channel) {

  uint16_t minLvl, maxLvl;

  // minLvl and maxLvl indicate the volume range over prior frames, used
  // for vertically scaling the output graph (so it looks interesting
  // regardless of volume level).  If they're too close together though
  // (e.g. at very low volume levels) the graph becomes super coarse
  // and 'jumpy'...so keep some minimum distance between them (this
  // also lets the graph go to zero when no sound is playing):
  if(channel == 0) {
    minLvl = maxLvl = volLeft[0];
    for (int i = 1; i < SAMPLES; i++) {
      if (volLeft[i] < minLvl) minLvl = volLeft[i];
      else if (volLeft[i] > maxLvl) maxLvl = volLeft[i];
    }
    if ((maxLvl - minLvl) < TOP) maxLvl = minLvl + TOP;
    
    minLvlAvgLeft = (minLvlAvgLeft * 63 + minLvl) >> 6; // Dampen min/max levels
    maxLvlAvgLeft = (maxLvlAvgLeft * 63 + maxLvl) >> 6; // (fake rolling average)
  }

  else {
    minLvl = maxLvl = volRight[0];
    for (int i = 1; i < SAMPLES; i++) {
      if (volRight[i] < minLvl) minLvl = volRight[i];
      else if (volRight[i] > maxLvl) maxLvl = volRight[i];
    }
    if ((maxLvl - minLvl) < TOP) maxLvl = minLvl + TOP;
    minLvlAvgRight = (minLvlAvgRight * 63 + minLvl) >> 6; // Dampen min/max levels
    maxLvlAvgRight = (maxLvlAvgRight * 63 + maxLvl) >> 6; // (fake rolling average)
  }
}


void dropPeak(uint8_t channel) {
 
  if(channel == 0) {
    if(++dotCountLeft >= PEAK_FALL) { //fall rate 
      if(peakLeft > 0) peakLeft--;
      dotCountLeft = 0;
    }
  } else {
    if(++dotCountRight >= PEAK_FALL) { //fall rate 
      if(peakRight > 0) peakRight--;
      dotCountRight = 0;
    }
  }
}

void vu5(uint8_t channel) {

  CRGB* leds;
  uint8_t i = 0;
  uint8_t *peak;      // Pointer variable declaration
  uint16_t height = auxReading(channel);

  if(channel == 0) {
    leds = ledsL;    // Store address of peakLeft in peak, then use *peak to
    peak = &peakLeft;   // access the value of that address
  }
  else {
    leds = ledsR;
    peak = &peakRight;
  }

  if (height > *peak)
    *peak = height; // Keep 'peak' dot at top

    // Color pixels based on old school green/red vu
    for (uint8_t i = 0; i < N_PIXELS; i++) {
      if (i >= height) leds[i] = CRGB::Black;
      else if (i > N_PIXELS - (N_PIXELS / 3)) leds[i] = CRGB::Red;
      else leds[i] = CRGB::Red;
    }
  
    // Draw peak dot
    if (*peak > 0 && *peak <= N_PIXELS - 1){
      if (*peak > N_PIXELS - (N_PIXELS / 3)) leds[*peak] = CRGB::Red;
      else leds[*peak] = CRGB::Red;
    }
  
  dropPeak(channel);

  averageReadings(channel);

  FastLED.show();
}

// Delivers our webpage to the client
String SendHTML(){
  String page = "<!doctype html>";
  page += "<html lang=\"en\" class=\"h-100\">";
  page += "<head>";
  page += "<meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  page += "<meta name=\"Agent\" content=\"SecretAgentMan\">";
  page += "<title>VUME: Stero VU Meter</title>";
  page += "<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\" integrity=\"sha384-ggOyR0iXCbMQv3Xipma34MD+dH/1fQ784/j6cY/iJTQUOhcWr7x9JvoRxT2MZw1T\" crossorigin=\"anonymous\">";
  page += "</head>";
  page += "<body class=\"container d-flex h-100 text-center text-white bg-dark\">";
  page += "<div class=\"container d-flex w-25 h-auto p-3 mx-auto flex-column\">";
  page += "<header class=\"mb-5\">";
  page += "<div class=\"h-50 mb-5\">";
  page += "<h3 class=\"float-md-start mb-0\">VUME</h3></div></header>";
  page += "<h1 class=\"mt-5\">VUME</h1>";
  page += "<p class=\"text-left\" id=\"LED_Status\" style=\"margin: 0em\">LED Status: OFF</p>";
  page += "<p class=\"text-left\" id=\"LED_Color\" style=\"margin: 0em\">Color: </p>";
  page += "<p class=\"text-left\" id=\"LED_Mode\">Mode: OFF</p>";
  page += "<a href=\"#\" id=\"Power\" class=\"h6 h-10 btn btn lg btn-secondary fw-bold border-white bg-white text-dark\">ON/OFF</a>";
  page += "<input type=\"color\" style=\"margin-top: 1em; margin-bottom: 1em\" class=\"form-control form-control-md\" id=\"rgb2\" value=\"#563d7c\" title=\"Choose your color\">";
  page += "<a href=\"#\" class=\"h6 btn btn-lg btn-secondary fw-bold border-white bg-white dropdown-toggle text-dark\" id=\"Mode_Select\" data-toggle=\"dropdown\" aria-haspopup=\"true\" aria-expanded=\"false\">Mode Select</a>";
  page += "<div class=\"h6 h-10 dropdown-menu\" aria-labelledby=\"dropdownMenuButton\">";
  page += "<a href=\"#\" class=\"h6 dropdown-item\">Solid</a>";
  page += "<a href=\"#\" class=\"h6 dropdown-item\">VU Meter</a></div>";
  page += "<label for=\"Speed\" class=\"h6 text-left\">Speed: </label>";
  page += "<input type=\"range\" class=\"h6\" id=\"Speed\" min=\"5\" max=\"100\" value=\"75\" step=\"5\">";
  page += "<label for=\"Brightness\" class=\"h6 text-left\">Brightness: </label>";
  page += "<input type=\"range\" class=\"h6\" id=\"Brightness\" min=\"0\" max=\"100\" value=\"50\" step=\"5\"></div>";
  
  page += "<script src=\"https://code.jquery.com/jquery-3.6.0.min.js\"></script>";
  page += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\" integrity=\"sha384-UO2eT0CpHqdSJQ6hJty5KVphtPhzWj9WO1clHTMGa3JDZwrnQq4sF86dIHNDz0W1\" crossorigin=\"anonymous\"></script>";
  page += "<script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\" integrity=\"sha384-JjSmVgyd0p3pXB1rRibZUAYoIIy6OrQ6VrjIEaFf/nJGzIxFDsf4x0xIM+B07jRM\" crossorigin=\"anonymous\"></script></body></hmml>";

  page += "<script>$(\"#Power\").click(function(){if($(\"#LED_Status\").text() == \"LED Status: OFF\"){$(\"#LED_Status\").text(\"LED Status: ON\");$(\"#LED_Mode\").text(\"Mode: Solid\");}else{$(\"#LED_Status\").text(\"LED Status: OFF\");$(\"#LED_Mode\").text(\"Mode: OFF\");}});</script>";
  page += "<script>$(\"#Speed\").on(\"change\", function() {$(\"label:first\").text(\"Speed: \" + this.value);});</script>";
  page += "<script>$(\"#Brightness\").on(\"change\", function() {$(\"label:last\").text(\"Brightness: \" + this.value);});</script>";
  page += "<script>$(\"#rgb2\").on(\"change\", function(){$(\"#LED_Color\").text(\"Color: \" + this.value);$.post(\"\\SetColor\", this.value);});</script>";
  page += "<script>$(\".dropdown-item\").click(function(){$(\"#LED_Mode\").text(\"Mode: \" + this.text);});</script></body></html>";
  
  return page;
}
