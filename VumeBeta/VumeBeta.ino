#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <FastLED.h>

/*
 * Gradient palette "bhw1_28_gp", originally from
 * http://soliton.vm.bytemark.co.uk/pub/cpt-city/bhw/bhw1/tn/bhw1_28.png.index.html
 * Size: 32 bytes of program space.
*/
DEFINE_GRADIENT_PALETTE( bhw1_28_gp ) {
    0,  75,  1,221,
   30, 252, 73,255,
   48, 169,  0,242,
  119,   0,149,242,
  170,  43,  0,242,
  206, 252, 73,255,
  232,  78, 12,214,
  255,   0,149,242};

const char* ssid = "";
const char* password = "";

// Globals for power indicator /////////////////////
#define ON D0 // ESP8266 Pin to which onboard LED is connected
unsigned long previousMillis = 0;  // will store last time LED was updated
const int interval = 1000;  // interval at which to blink (milliseconds)
bool ledState = LOW;  // ledState used to set the LED
////////////////////////////////////////////////////

#define N_PIXELS 148                            // Number of LEDS
#define MAX_MA 2000                             // Max amps pulled
#define COLOR_ORDER GRB                         // Color order for the strip
#define LED_TYPE WS2812B                        // LED type
#define D_P1 D2                                 // Data out for Left
#define D_P2 D1                                 // Data out for Right
#define DC_OFFSET 0                             // Offset for dirty aux signal
#define NOISE 20                                // Noise value to filter out
#define SAMPLES 60                              // Length of buffer for dynamic adjustments
#define TOP (N_PIXELS + 2)                      // Top bar max height goes off sccreen a little
#define PEAK_FALL 20                            // Fall speed for top bar
#define N_PIXELS_HALF (N_PIXELS/2)
#define ANALOG A0                               // Analog pin for our reaings. We will have to multiplex it with nodemcu
#define LEFT_SIGNAL D6                          // Set this pin to high to set multiplexer to LEFT, and read LEFT signal
#define RIGHT_SIGNAL D7                         // Set this pin to high to set multiplexer to RIGHT, and read RIGHT signal

bool Power = false;
uint8_t animationSpeed = 0;
uint8_t Brightness = 0;
CRGB currentColor; 
String currentEffect = "Solid";                   // Our mode control

// GLOBALS FOR VU METER
int lvlLeft, lvlRight = 0;                      // Current "dampened" audio level
uint8_t peakLeft, peakRight;                    // Peak Levels
static uint8_t dotCountLeft, dotCountRight;     // idk tbh

// GLOBALS FOR RAINBOW ANIMATION
uint8_t thishue = 0;                                  
uint8_t deltahue = 10;

// GLOBALS FOR CANDYCANE ANIMATION
CRGBPalette16 currentPalettestriped; 

// GLOABLS FOR NOISE ANIMATION
static uint16_t dist;                            // A random number for our noise generator.
uint16_t scale = 60;                             // Wouldn't recommend changing this on the fly, or the animation will be really blocky.
uint8_t maxChanges = 48;                         // Value for blending between palettes.
CRGBPalette16 targetPalette(bhw1_28_gp);         // Palette we are blending towards
CRGBPalette16 currentPalette(CRGB::Black);       // Our empty black palette

ESP8266WebServer server(80);                     // global for server on port 80
CRGB ledsL[N_PIXELS];                            // Left LED strip
CRGB ledsR[N_PIXELS];                            // Right LED strip


/*
 * Function that runs at startup. Connects wifi, initializes server, 
 * creates LED strips, initializes OTA, defines our pin modes
 */
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
  server.on("/SetMode", onSetMode);
  server.onNotFound(notFound);                              // Binds notFound handler to 404 error

  server.begin();
  Serial.println("HTTP server started");
  ///////////// DONT TOUCH ABOVE //////////////////////////////////

  FastLED.addLeds<LED_TYPE, D_P1, COLOR_ORDER>(ledsL, N_PIXELS);         // Adds left strip to our FastLED workspace
  FastLED.addLeds<LED_TYPE, D_P2, COLOR_ORDER>(ledsR, N_PIXELS);         // Adds right strip to our FastLED workspace

  setupStripedPalette( CRGB::Red, CRGB::Red, CRGB::White, CRGB::White);

  pinMode(ANALOG, INPUT);
}
/*
 * Loop function, essentially main but looped. First few lines simply flash pin D0
 * letting us know the board turned on and connected to wifi. Then we check pattern for
 * for the current mode and run the animation, or do nothing if solid.
 */
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
  if(Power){
    
    if(currentEffect == "Rainbow"){
      thishue++;
      fill_rainbow(ledsL, N_PIXELS, thishue, deltahue);
      fill_rainbow(ledsR, N_PIXELS, thishue, deltahue);
      if (animationSpeed == 0 or animationSpeed == NULL) {
        animationSpeed = 130;
      }
      showleds();  
    }
    
    if(currentEffect == "CandyCane"){
      static uint8_t startIndex = 0;
      startIndex = startIndex + 1; 
      fill_palette( ledsL, N_PIXELS, startIndex, 16, currentPalettestriped, 255, LINEARBLEND);
      fill_palette( ledsR, N_PIXELS, startIndex, 16, currentPalettestriped, 255, LINEARBLEND);
      if (animationSpeed == 0 or animationSpeed == NULL) {
        animationSpeed = 0;
      }
      showleds();
    }

    EVERY_N_MILLISECONDS(10){
    
      nblendPaletteTowardPalette(currentPalette, targetPalette, maxChanges);
    
      if(currentEffect == "Noise"){
        for (int i = 0; i < N_PIXELS; i++) {                                     // Just onE loop to fill up the LED array as all of the pixels change.
          uint8_t index = inoise8(i * scale, dist + i * scale) % 255;            // Get a value from the noise function. I'm using both x and y axis.
          ledsL[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);   // With that value, look up the 8 bit colour palette value and assign it to the current LED.
          ledsR[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);
        }
        dist += beatsin8(10, 1, 4);                                              // Moving along the distance (that random number we started out with). Vary it a bit with a sine wave.
        if (animationSpeed == 0 or animationSpeed == NULL) {
          animationSpeed = 0;
        }
        showleds();
      }
    }  
  }
}

/*
 * When a user connects, display connect message and serve webpage
 */
void onConnect() {
  Serial.println("User has connected: ");
  server.send(200, "text/html", SendHTML());
}

/* 
 * When a user clicks the power button, get the current status and either turn the strip on or off 
 */
void onPower() {
  if(server.arg("Status") == "ON"){
    Serial.println("VUME turning on");
    Power = true;
    Brightness = server.arg("Brightness").toInt();
    animationSpeed = server.arg("Speed").toInt();
    Serial.print("Brightness: "); Serial.println(Brightness);
    Serial.print("Speed: "); Serial.println(animationSpeed);
    
    if(currentEffect == "Solid"){
      Serial.println("pointer check passed");
      setStripColor(CRGB::White);
    }
    
  }
  else{
    Serial.println("VUME turning off");
    Power = false;
    FastLED.clear();
    FastLED.show();
  }
  server.send(200, "text/html", SendHTML());                // Sends the client the new webpage, not sure if I need to keep this with the Jquery
}

/*
 * Receives a HTTP post message containing the hex value of the color requested by the user 
 * Converts it to CRGB and calls setStripColor, and setPattern
 */
void onSetColor() {
  String Value = "0x" + server.arg("Color").substring(1);           // Get the hex argument from the post message
  currentColor = strtol(Value.c_str(), NULL,16);
  Brightness = server.arg("Brightness").toInt();
  Power = true;
  Serial.println("Setting color to: ");
  Serial.println(Value);
  Serial.println(Brightness);
  setStripColor(currentColor);
  currentEffect = "Solid";
  Serial.println(currentEffect);
  server.send(200, "text/html", SendHTML());                // Sends the client the new webpage, not sure if I need to keep this with the JQuery
}

void onSetMode(){
  Serial.println("Setting Mode");
  Power = true;
  Brightness = server.arg("Brightness").toInt();
  animationSpeed = server.arg("Speed").toInt();
  currentEffect = server.arg("Mode");
  Brightness = server.arg("Brightness").toInt();
  animationSpeed = server.arg("Speed").toInt();
  Serial.println(currentEffect);
  server.send(200, "text/html", SendHTML());
}

/*
 * Wrong URL, tell client "Error 404"
 */
void notFound (){
  server.send(404, "text/plain", "Not found");              // Serve 404 message
}

/*
 * Loops through and sets both strips to the same color
 */
void setStripColor(CRGB color){
  for (uint8_t i =0; i < N_PIXELS; i++){
    ledsL[i] = color;
    ledsR[i] = color;
  }
  FastLED.show();
}

/*
 * The current state of the strip i.e. solid, rainbow, off
 */
void setEffect(char *newEffect){
  currentEffect = newEffect;
}

void setupStripedPalette( CRGB A, CRGB AB, CRGB B, CRGB BA) {
  currentPalettestriped = CRGBPalette16(A, A, A, A, A, A, A, A, B, B, B, B, B, B, B, B);
}

/*
 * Function for displaying our LED's at our determined speed
 */
void showleds() {
  delay(1);
  if (Power) {
    FastLED.setBrightness(Brightness);  //EXECUTE EFFECT COLOR
    FastLED.show();
    if (animationSpeed > 0 && animationSpeed < 130) {  //Sets animation speed based on receieved value
      FastLED.delay(1000 / animationSpeed);
    }
  }
}

uint16_t auxReading(uint8_t channel) {

  Serial.print("Reading Channel: ");
  Serial.println(channel);
  int n = 0;
  uint16_t height = 0;

  if(channel == 0) {
    pinMode(RIGHT_SIGNAL, INPUT);
    pinMode(LEFT_SIGNAL, OUTPUT);
    digitalWrite(LEFT_SIGNAL, LOW);
    int n = analogRead(ANALOG); // Raw reading from left line in
    Serial.println(n);
    delay(100);
    /*
    n = abs(n - 512 - DC_OFFSET); // Center on zero
    n = (n <= NOISE) ? 0 : (n - NOISE); // Remove noise/hum
    lvlLeft = ((lvlLeft * 7) + n) >> 3; // "Dampened" reading else looks twitchy (>>3 is divide by 8)
    */
    // Calculate bar height based on dynamic min/max levels (fixed point):
    // height = TOP * (lvlLeft - minLvlAvgLeft) / (long)(maxLvlAvgLeft - minLvlAvgLeft);
    height = map(n, 0, 400, 0, 150);
  }

  else {
    pinMode(LEFT_SIGNAL, INPUT);
    pinMode(RIGHT_SIGNAL, OUTPUT);
    digitalWrite(RIGHT_SIGNAL, LOW);
    int n = analogRead(ANALOG); // Raw reading from mic
    Serial.println(n);
    delay(100);
    /*
    n = abs(n - 512 - DC_OFFSET); // Center on zero
    n = (n <= NOISE) ? 0 : (n - NOISE); // Remove noise/hum
    lvlRight = ((lvlRight * 7) + n) >> 3; // "Dampened" reading (else looks twitchy)
    */
    // Calculate bar height based on dynamic min/max levels (fixed point):
    // height = TOP * (lvlRight - minLvlAvgRight) / (long)(maxLvlAvgRight - minLvlAvgRight);
    height = map(n, 0, 400, 0, 150);
  }

  // Calculate bar height based on dynamic min/max levels (fixed point):
  //height = constrain(height, 0, TOP);
  return height;
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
  uint8_t *peak;      // Pointer variable declaration
  uint16_t height = auxReading(channel);
  Serial.println("Setting Height for channel: ");
  Serial.println(height);
  Serial.println(channel);

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
      else leds[i] = CRGB::Red;
    }

    // Draw peak dot
    if (*peak > 0 && *peak <= N_PIXELS - 1){
      leds[*peak] = CRGB::Red;
    }

  dropPeak(channel);

  FastLED.show();
}

/*
 * Delivers our WebUI to the client
 */
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
  page += "<p class=\"text-left\" id=\"LED_Mode\">Mode: Solid</p>";
  page += "<a href=\"#\" id=\"Power\" class=\"h6 h-10 btn btn lg btn-secondary fw-bold border-white bg-white text-dark\">ON/OFF</a>";
  page += "<input type=\"color\" style=\"margin-top: 1em; margin-bottom: 1em\" class=\"form-control form-control-md\" id=\"rgb2\" value=\"#563d7c\" title=\"Choose your color\">";
  page += "<a href=\"#\" class=\"h6 btn btn-lg btn-secondary fw-bold border-white bg-white dropdown-toggle text-dark\" id=\"Mode_Select\" data-toggle=\"dropdown\" aria-haspopup=\"true\" aria-expanded=\"false\">Mode Select</a>";
  page += "<div class=\"h6 h-10 dropdown-menu\" aria-labelledby=\"dropdownMenuButton\">";
  page += "<a href=\"#\" class=\"h6 dropdown-item\">Noise</a>";
  page += "<a href=\"#\" class=\"h6 dropdown-item\">Rainbow</a>";
  page += "<a href=\"#\" class=\"h6 dropdown-item\">CandyCane</a></div>";
  page += "<label for=\"Speed\" class=\"h6 text-left\">Speed: </label>";
  page += "<input type=\"range\" class=\"h6\" id=\"Speed\" min=\"5\" max=\"100\" value=\"75\" step=\"5\">";
  page += "<label for=\"Brightness\" class=\"h6 text-left\">Brightness: </label>";
  page += "<input type=\"range\" class=\"h6\" id=\"Brightness\" min=\"0\" max=\"100\" value=\"50\" step=\"5\"></div>";

  page += "<script src=\"https://code.jquery.com/jquery-3.6.0.min.js\"></script>";
  page += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\" integrity=\"sha384-UO2eT0CpHqdSJQ6hJty5KVphtPhzWj9WO1clHTMGa3JDZwrnQq4sF86dIHNDz0W1\" crossorigin=\"anonymous\"></script>";
  page += "<script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\" integrity=\"sha384-JjSmVgyd0p3pXB1rRibZUAYoIIy6OrQ6VrjIEaFf/nJGzIxFDsf4x0xIM+B07jRM\" crossorigin=\"anonymous\"></script></body></hmml>";

  page += "<script>$(\"#Power\").click(function(){if($(\"#LED_Status\").text() == \"LED Status: OFF\"){$(\"#LED_Status\").text(\"LED Status: ON\");$.post(\"\\Power\", { Status: \"ON\", Brightness: $(\"#Brightness\").val(), Speed: $(\"#Speed\").val()});}else{$(\"#LED_Status\").text(\"LED Status: OFF\");$.post(\"\\Power\", { Status: \"OFF\"});}});</script>";
  page += "<script>$(\"#Speed\").on(\"change\", function() {$(\"label:first\").text(\"Speed: \" + this.value);});</script>";
  page += "<script>$(\"#Brightness\").on(\"change\", function() {$(\"label:last\").text(\"Brightness: \" + this.value);});</script>";
  page += "<script>$(\"#rgb2\").on(\"change\", function(){$(\"#LED_Color\").text(\"Color: \" + this.value);$(\"#LED_Mode\").text(\"Mode: Solid\");$.post(\"\\SetColor\", {Color: this.value, Brightness: $(\"#Brightness\").val()});});</script>";
  page += "<script>$(\".dropdown-item\").click(function(){$(\"#LED_Mode\").text(\"Mode: \" + this.text);$.post(\"\\SetMode\", {Mode: this.text, Brightness: $(\"#Brightness\").val(), Speed: $(\"#Speed\").val()} );});</script></body></html>";

  return page;
}
