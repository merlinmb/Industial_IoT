#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <SFE_BMP180.h>
#include <Wire.h>
#include <SimpleTimer\SimpleTimer.h>

#define DEBUG 1

#ifdef DEBUG
#define DEBUG_PRINT(x)			Serial.print (x)
#define DEBUG_PRINTDEC(x,DEC)	Serial.print (x, DEC)
#define DEBUG_PRINTLN(x)		Serial.println (x)
#define DEBUG_PRINTLNDEC(x,DEC)	Serial.println (x, DEC)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTDEC(x,DEC)
#define DEBUG_PRINTLN(x) 
#define DEBUG_PRINTLNDEC(x,DEC)
#endif


#define ALTITUDE 1555.0 // Altitude of SparkFun's HQ in Boulder, CO. in meters
// this constant won't change:
const int RESETBUTTONPIN = 13;    // the pin that the pushbutton is attached t

const char* _localWebHost = "IIoT";
const char* _updateWebPath = "/firmware";
const char* _updateWebUsername = "admin";
const char* _updateWebPassword = "admin";

SFE_BMP180 _bmp180Sensor;
SimpleTimer _sensorTimer;

String _webClientReturnString = "404: Nothing to see here!";     // String to send to web clients
bool _isConnected = false;
bool _shouldSaveConfigWM = false;

char _sensorStatus;
double _temperature, _pressure, _humidity, _relativePressure, _altitude;

ESP8266WebServer _httpServer(80);
ESP8266HTTPUpdateServer _httpUpdater;

//debounce the button press
long _debouncingTime = 100; //Debouncing Time in Milliseconds
volatile unsigned long _lastMicros;

void configModeCallback(WiFiManager *myWiFiManager) {
	DEBUG_PRINTLN("Entered config mode");
	DEBUG_PRINTLN(WiFi.softAPIP());
	//if you used auto generated SSID, print it
	DEBUG_PRINTLN(myWiFiManager->getConfigPortalSSID());
	
}

//callback notifying us of the need to save config
void saveConfigCallback() {
	Serial.println("Should save config");
	_shouldSaveConfigWM = true;
}

void setupWifi() {
	//WiFiManager
	//Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;

	//set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
	wifiManager.setAPCallback(configModeCallback);
	wifiManager.setSaveConfigCallback(saveConfigCallback);
	wifiManager.setMinimumSignalQuality(25);
	wifiManager.setRemoveDuplicateAPs(true);

	// id/name, placeholder/prompt, default, length
	WiFiManagerParameter __smsWMParam("mobile", "Alert mobile number", "+27", 40);
	wifiManager.addParameter(&__smsWMParam);
	WiFiManagerParameter __thWMParam("temphigh", "Temperature trigger (high)", "25", 8);
	wifiManager.addParameter(&__thWMParam);
	WiFiManagerParameter __tlWMParam("templow", "Temperature trigger (low)", "+27", 8);
	wifiManager.addParameter(&__tlWMParam);
	WiFiManagerParameter __hhWMParam("humidityhigh", "Humidity trigger (high)", "+27", 8);
	wifiManager.addParameter(&__hhWMParam);
	WiFiManagerParameter __hlWMParam("humiditylow", "Humidity trigger (low)", "+27", 8);
	wifiManager.addParameter(&__hlWMParam);
	WiFiManagerParameter __retOffWMParam("return", "Off when value returns to % of trigger", "5", 8);
	wifiManager.addParameter(&__retOffWMParam);
	WiFiManagerParameter __timerWMParam("timer", "Run every X minutes", "1", 8);
	wifiManager.addParameter(&__timerWMParam);

	//sets timeout until configuration portal gets turned off
	//useful to make it all retry or go to sleep
	//in seconds
	wifiManager.setTimeout(90);

	//fetches ssid and pass and tries to connect
	//if it does not connect it starts an access point with the specified name
	//here  "AutoConnectAP"
	//and goes into a blocking loop awaiting configuration

	String __apName = "Industrial_IoT_" + String(ESP.getChipId());
	char __apNameCA[__apName.length() + 1];
	__apName.toCharArray(__apNameCA, __apName.length());

	String __websiteHostName = String("IIoT_" + String(ESP.getChipId()));
	//_localWebHost = __websiteHostName.c_str();

	DEBUG_PRINT("Wifi AP Mode. Broadcasting as:");
	DEBUG_PRINTLN(__apName);

	if (!wifiManager.autoConnect(__apNameCA)) {
		DEBUG_PRINTLN("failed to connect and hit timeout");
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(1000);
	}

	//if you get here you have connected to the WiFi
	DEBUG_PRINTLN("Connected... :)");
	delay(1500);

	WiFi.mode(WIFI_AP_STA);
	MDNS.begin(_localWebHost);

	_isConnected = true;
}

void setupBMP180() {
	
	DEBUG_PRINTLN();
	DEBUG_PRINT("Provided altitude: ");
	DEBUG_PRINTDEC(ALTITUDE, 0);
	DEBUG_PRINT(" meters, ");
	DEBUG_PRINTDEC(ALTITUDE*3.28084, 0);
	DEBUG_PRINTLN(" feet");

	// Initialize the sensor (it is important to get calibration values stored on the device).
	Wire.begin(4, 5); //(sda, scl)
	delay(10);
	if (_bmp180Sensor.begin())
		Serial.println("BMP180 init successful!");
	else
	{
		// Oops, something went wrong, this is usually a connection problem,
		Serial.println("BMP180 init fail, values will be zeroed (0)");
	}
}

void Interrupt() {
	DEBUG_PRINTLN("Button Pressed");
	WiFiManager wifiManager;
	delay(50);

	wifiManager.resetSettings();
	delay(1000);
	ESP.reset();
}

void debounceInterrupt() {
	if ((long)(micros() - _lastMicros) >= _debouncingTime * 1000) {
		Interrupt();
		_lastMicros = micros();
	}
}

void setupHTTPUpdateServer() {

	_httpUpdater.setup(&_httpServer, _updateWebPath, _updateWebUsername, _updateWebPassword);

	MDNS.addService("http", "tcp", 80);
	Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", _localWebHost, _updateWebPath, _updateWebUsername, _updateWebPassword);
}

void handle_root() {
	_webClientReturnString = "Hello from Industrial IoT, read individual values from /temperature /humidity /pressure \n\n";
	//_webClientReturnString += "Humidity: " + String((int)_humidity) + "%" + " ----  Temperature: " + String((int)_temperature) + "C" + " ---- Pressure: " + String((int)_pressure) + "mb";
	_webClientReturnString += "Temperature: " + String((int)_temperature) + "C" + " ---- Pressure: " + String((int)_pressure) + "mb";

	_httpServer.send(200, "text/plain", _webClientReturnString);
	delay(100);
}

void setupWebServer() {
	_httpServer.on("/", &handle_root);
	_httpServer.on("/temperature", []() {  // if you add this subdirectory to your webserver call, you get text below :)
		_webClientReturnString = "Temperature: " + String((int)_temperature) + "C";   // Arduino has a hard time with float to string
		_httpServer.send(200, "text/plain", _webClientReturnString);            // send to someones browser when asked
	});

	_httpServer.on("/humidity", []() {  // if you add this subdirectory to your webserver call, you get text below :)
		//gettemperature();           // read sensor
		_webClientReturnString = "Humidity: " + String((int)_humidity) + "%";
		_httpServer.send(200, "text/plain", _webClientReturnString);               // send to someones browser when asked
	});

	_httpServer.on("/pressure", []() {  // if you add this subdirectory to your webserver call, you get text below :)
										//gettemperature();           // read sensor
		_webClientReturnString = "Relative Pressure: " + String((int)_relativePressure) + "mb";
		_httpServer.send(200, "text/plain", _webClientReturnString);               // send to someones browser when asked
	});
}

void updateSensorValues() {
	// If you want to measure altitude, and not pressure, you will instead need
	// to provide a known baseline pressure. This is shown at the end of the sketch.

	// You must first get a temperature measurement to perform a pressure reading.

	// Start a temperature measurement:
	// If request is successful, the number of ms to wait is returned.
	// If request is unsuccessful, 0 is returned.

	_sensorStatus = _bmp180Sensor.startTemperature();
	if (_sensorStatus != 0)
	{
		// Wait for the measurement to complete:
		delay(_sensorStatus);

		// Retrieve the completed temperature measurement:
		// Note that the measurement is stored in the variable T.
		// Function returns 1 if successful, 0 if failure.

		//_sensorStatus = _bmp180Sensor.gethumidity;
		
		_sensorStatus = _bmp180Sensor.getTemperature(_temperature);
		if (_sensorStatus != 0)
		{
			// Print out the measurement:
			Serial.print("temperature: ");
			Serial.print(_temperature, 2);
			Serial.print(" deg C, ");
			Serial.print((9.0 / 5.0)*_temperature + 32.0, 2);
			Serial.println(" deg F");

			// Start a pressure measurement:
			// The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
			// If request is successful, the number of ms to wait is returned.
			// If request is unsuccessful, 0 is returned.

			_sensorStatus = _bmp180Sensor.startPressure(3);
			if (_sensorStatus != 0)
			{
				// Wait for the measurement to complete:
				delay(_sensorStatus);

				// Retrieve the completed pressure measurement:
				// Note that the measurement is stored in the variable P.
				// Note also that the function requires the previous temperature measurement (T).
				// (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
				// Function returns 1 if successful, 0 if failure.

				_sensorStatus = _bmp180Sensor.getPressure(_pressure, _temperature);
				if (_sensorStatus != 0)
				{
					// Print out the measurement:
					Serial.print("absolute pressure: ");
					Serial.print(_pressure, 2);
					Serial.print(" mb, ");
					Serial.print(_pressure*0.0295333727, 2);
					Serial.println(" inHg");

					// The pressure sensor returns abolute pressure, which varies with altitude.
					// To remove the effects of altitude, use the sealevel function and your current altitude.
					// This number is commonly used in weather reports.
					// Parameters: P = absolute pressure in mb, ALTITUDE = current altitude in m.
					// Result: p0 = sea-level compensated pressure in mb

					_relativePressure = _bmp180Sensor.sealevel(_pressure, ALTITUDE); // we're at 1655 meters (Boulder, CO)
					Serial.print("relative (sea-level) pressure: ");
					Serial.print(_relativePressure, 2);
					Serial.print(" mb, ");
					Serial.print(_relativePressure*0.0295333727, 2);
					Serial.println(" inHg");

					// On the other hand, if you want to determine your altitude from the pressure reading,
					// use the altitude function along with a baseline pressure (sea-level or other).
					// Parameters: P = absolute pressure in mb, p0 = baseline pressure in mb.
					// Result: a = altitude in m.

					_altitude = _bmp180Sensor.altitude(_pressure, _relativePressure);
					Serial.print("computed altitude: ");
					Serial.print(_altitude, 0);
					Serial.print(" meters, ");
					Serial.print(_altitude*3.28084, 0);
					Serial.println(" feet");
				}
				else Serial.println("error retrieving pressure measurement\n");
			}
			else Serial.println("error starting pressure measurement\n");
		}
		else Serial.println("error retrieving temperature measurement\n");
	}
	else Serial.println("error starting temperature measurement\n");

}

void setup(void) {

	Serial.begin(115200);
	DEBUG_PRINTLN();
	DEBUG_PRINTLN("Booting Sketch...");
	
	pinMode(RESETBUTTONPIN, INPUT);
	attachInterrupt(RESETBUTTONPIN, debounceInterrupt, RISING);
	

	setupWifi(); //blocking function

	setupBMP180();
	//run it once:
	updateSensorValues();
	_sensorTimer.setInterval(30000, updateSensorValues);

	setupHTTPUpdateServer();
	setupWebServer();

	_httpServer.begin();
}

void loop(void) {
	if(_isConnected)
	{ 
		_httpServer.handleClient();
		_sensorTimer.run();
	}
}