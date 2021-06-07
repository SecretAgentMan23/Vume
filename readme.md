# Vume

Vume is a RGB Stereo VU Meter designed for the Nodemcu esp8266 microcontroller with the WS2812B LED Strips.

After startup, navigate to the ip address set by Vume to load the Web Interface. From there, you can control the color of the LED strips, brightness, and set other animations!

Supported Features include:
  - Web User Interface
  - RGB Color Selection
  - Volume Unit Meter
  - Various Animations
  - Change Brightness
  - Change Animation Speed
  - OTA Updates

## Installation and Usage

Vume is completely open source and hackable so feel free to make your own! To build your install the [FastLED](https://github.com/FastLED/FastLED) library and the [ESP8266 board manager](https://arduino-esp8266.readthedocs.io/en/latest/installing.html).

Download and open the code as we need to make some modifications.

Add your Wifi SSID and Password here
```c++
const char* ssid = "";
const char* password = "";

```
The code below defines the the number of LEDs being used minus a few for the peak bar, the color order of the strip(RGB, BRG, GRB, etc..), and the LED strip type. "Noise" filters out noise from our dirty 3.5mm aux signal, and peak_fall is the fall speed of the peak dot if drawn.
```c++
#define N_PIXELS 148               
#define COLOR_ORDER GRB            
#define LED_TYPE WS2812B                                          
#define NOISE 20                      
#define PEAK_FALL 20                        
```

## ArduinoOTA

This project does support ArduinoOTA. If you are unfamiliar with OTA, you can read more about it [here](https://www.arduino.cc/reference/en/libraries/arduinoota/)!

Change the name of your microcontroller, and the password for pushing updates
```c++
ArduinoOTA.setHostname("ESP8266");
ArduinoOTA.setPassword("ChangeMe");
```

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.


## Credits

This project was heavily based off of a similar project found [here](https://github.com/s-marley/Uno_vu_line) by Scott Marley.
