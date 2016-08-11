#include <FS.h>						//this needs to be first, or it all crashes and burns...

#include <ArduinoJson.h>			//https://github.com/bblanchon/ArduinoJson

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>			// https://github.com/tzapu/WiFiManager
#include <SFE_BMP180.h>
#include <Wire.h>
#include <SimpleTimer.h>
#include <Ticker.h>
#include <Time.h>
#include <QueueArray.h>

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

struct configValues {
	char SMS[12] = "+27";		// Alert mobile number
	int TemperatureHigh = 25;		// Temperature trigger (high)
	int TemperatureLow = 5;		// Temperature trigger (low)
	int HumidityHigh = 80;		// Humidity trigger (high)
	int HumidityLow = 10;		// Humidity trigger (low)
	int ReturnToOffPercentage = 5;	// Off when value returns to % of trigger
	int CheckBoundsTimerMinutes = 1;	// default trigger is ever minute
};


struct sensorValueStruct {
	time_t Time;  //umber of seconds since the epoch, usage: DateTime dt = RTC.now(); time = dt.unixtime();
	double Temperature, Pressure, Humidity, RelativePressure, Altitude, CO;
};

configValues _currentConfigValues;
sensorValueStruct _currentSensorValues;
const int SENSORVALUESWIDTH = 50;
int _previousSensorValuesCount = 0;
sensorValueStruct _previousSensorValues[SENSORVALUESWIDTH];

SFE_BMP180 _bmp180Sensor;
SimpleTimer _sensorTimer;
Ticker _tickerOnBoardLED;

String _webClientReturnString = "404: Nothing to see here!";     // String to send to web clients
bool _isConnected = false;
bool _shouldSaveConfigWM = false;


ESP8266WebServer _httpServer(80);
ESP8266HTTPUpdateServer _httpUpdater;

//debounce the button press
long _debouncingTime = 100; //Debouncing Time in Milliseconds
volatile unsigned long _lastMicros;

void drawGraph() {
	String out = "";
	char temp[100];
	out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
	out += "<rect width=\"" + String(SENSORVALUESWIDTH*10) + """\ height=\"500\" fill=\"rgb(240, 240, 240)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
	out += "<g stroke=\"black\">\n";
	if (_previousSensorValuesCount>=2){
		for (int x = 0; x < _previousSensorValuesCount-1; x++) {
			int y = (int)(_previousSensorValues[x].Temperature*5);
			int y2 = (int)(_previousSensorValues[x+1].Temperature*5);
			sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", (x)*10, 100 - y, (x+1) * 10, 100 - y2);
			out += temp;
			y = y2;
		}
	}
	out += "</g>\n</svg>\n";

	_httpServer.send(200, "image/svg+xml", out);
}

void tickLED()
{
	//toggle state
	int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
	digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state
}

void configModeCallback(WiFiManager *myWiFiManager) {
	DEBUG_PRINTLN("Entered config mode");
	DEBUG_PRINTLN(WiFi.softAPIP());
	//if you used auto generated SSID, print it
	DEBUG_PRINTLN(myWiFiManager->getConfigPortalSSID());

}

//callback notifying us of the need to save config
void saveConfigCallback() {
	DEBUG_PRINTLN("Should save config");
	_shouldSaveConfigWM = true;
}

void loadCustomParamsSPIFFS() {
	//read configuration from FS json
	DEBUG_PRINTLN("Mounting FS...");

	if (SPIFFS.begin()) {
		DEBUG_PRINTLN("mounted file system");
		if (SPIFFS.exists("/config.json")) {
			//file exists, reading and loading
			DEBUG_PRINTLN("reading config file");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
				DEBUG_PRINT("opened config file: ");
				size_t size = configFile.size();
				DEBUG_PRINT(String(size)); DEBUG_PRINTLN(" bytes");
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
				json.printTo(Serial);
				if (json.success()) {
					DEBUG_PRINTLN("\nparsed json");

					strcpy(_currentConfigValues.SMS, json["mobile"]);
					_currentConfigValues.TemperatureHigh = json["temphigh"];
					_currentConfigValues.TemperatureLow = json["templow"];
					_currentConfigValues.HumidityHigh = json["humidityhigh"];
					_currentConfigValues.HumidityLow = json["humiditylow"];
					_currentConfigValues.ReturnToOffPercentage = json["return"];
					_currentConfigValues.CheckBoundsTimerMinutes = json["timer"];
				}
				else {
					DEBUG_PRINTLN("failed to load json config");
				}
			}
		}
	}
	else {
		DEBUG_PRINTLN("failed to mount FS");
	}
	//end read
}

void saveConfigValuesSPIFFS() {
	if (_shouldSaveConfigWM) {
		DEBUG_PRINTLN("Saving config");
		DynamicJsonBuffer jsonBuffer;
		JsonObject& json = jsonBuffer.createObject();

		json["mobile"] = _currentConfigValues.SMS;
		json["temphigh"] = _currentConfigValues.TemperatureHigh;
		json["templow"] = _currentConfigValues.TemperatureLow;
		json["humidityhigh"] = _currentConfigValues.HumidityLow;
		json["humiditylow"] = _currentConfigValues.HumidityLow;
		json["return"] = _currentConfigValues.ReturnToOffPercentage;
		json["timer"] = _currentConfigValues.CheckBoundsTimerMinutes;


		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile) {
			DEBUG_PRINTLN("failed to open config file for writing");
		}
		else {
			json.printTo(Serial);
			json.printTo(configFile);
			configFile.close();
			//end save
			DEBUG_PRINTLN("Saving config completed");
		}
	}
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

	loadCustomParamsSPIFFS();

	char __th[8], __tl[8], __hh[8], __hl[8], __retOff[8], __timer[8];

	//Custom parameters:  (id/name, placeholder/prompt, default, length)
	WiFiManagerParameter __smsWMParam("mobile", "Alert mobile number", _currentConfigValues.SMS, 12);
	wifiManager.addParameter(&__smsWMParam);

	WiFiManagerParameter __thWMParam("temphigh", "Temperature trigger (high)", itoa(_currentConfigValues.TemperatureLow, __th, 8), 8);
	wifiManager.addParameter(&__thWMParam);

	WiFiManagerParameter __tlWMParam("templow", "Temperature trigger (low)", itoa(_currentConfigValues.TemperatureLow, __tl, 8), 8);
	wifiManager.addParameter(&__tlWMParam);

	WiFiManagerParameter __hhWMParam("humidityhigh", "Humidity trigger (high)", itoa(_currentConfigValues.HumidityHigh, __hh, 8), 8);
	wifiManager.addParameter(&__hhWMParam);

	WiFiManagerParameter __hlWMParam("humiditylow", "Humidity trigger (low)", itoa(_currentConfigValues.HumidityLow, __hl, 8), 8);
	wifiManager.addParameter(&__hlWMParam);

	WiFiManagerParameter __retOffWMParam("return", "Off when value returns to % of trigger", itoa(_currentConfigValues.ReturnToOffPercentage, __retOff, 8), 8);
	wifiManager.addParameter(&__retOffWMParam);

	WiFiManagerParameter __timerWMParam("timer", "Run every X minutes", itoa(_currentConfigValues.CheckBoundsTimerMinutes, __timer, 8), 8);
	wifiManager.addParameter(&__timerWMParam);


	//sets timeout until configuration portal gets turned off
	//useful to make it all retry or go to sleep
	//in seconds
	wifiManager.setTimeout(360);

	//fetches ssid and pass and tries to connect
	//if it does not connect it starts an access point with the specified name
	//here  "AutoConnectAP"
	//and goes into a blocking loop awaiting configuration

	String __apName = "Industrial_IoT_" + String(ESP.getChipId());
	char __apNameCA[(__apName.length() + 1)];
	__apName.toCharArray(__apNameCA, __apName.length());

	String __websiteHostName = String("IIoT_" + String(ESP.getChipId()));
	//_localWebHost = __websiteHostName.c_str();

	DEBUG_PRINT("Wifi AP Mode. Broadcasting as:");
	DEBUG_PRINTLN(__apName);

	if (!wifiManager.autoConnect(__apNameCA)) {
		DEBUG_PRINTLN("failed to connect and hit timeout");
		//reset and try again, or maybe put it to deep sleep
		ESP.restart();
		delay(1000);
	}

	//if you get here you have connected to the WiFi
	DEBUG_PRINTLN("Connected... :)");

	strcpy(_currentConfigValues.SMS, __smsWMParam.getValue());
	_currentConfigValues.TemperatureHigh = atoi(__thWMParam.getValue());
	_currentConfigValues.TemperatureLow = atoi(__tlWMParam.getValue());
	_currentConfigValues.HumidityHigh = atoi(__hhWMParam.getValue());
	_currentConfigValues.HumidityLow = atoi(__hlWMParam.getValue());
	_currentConfigValues.ReturnToOffPercentage = atoi(__retOffWMParam.getValue());
	_currentConfigValues.CheckBoundsTimerMinutes = atoi(__timerWMParam.getValue());

	saveConfigValuesSPIFFS();		//write the values to FS

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
		DEBUG_PRINTLN("BMP180 init successful!");
	else
	{
		// Oops, something went wrong, this is usually a connection problem,
		DEBUG_PRINTLN("BMP180 init fail, values will be zeroed (0)");
	}
}

void Interrupt() {
	if (_isConnected)
	{

		WiFiManager wifiManager;
		delay(50);

		DEBUG_PRINTLN("Resetting WifiManager Settings");
		wifiManager.resetSettings();
		DEBUG_PRINTLN("Formatting Filesystem, please wait... (30 seconds)");
		SPIFFS.format();

		delay(500);
		DEBUG_PRINTLN("Completed. Resetting IIoT.");
		ESP.restart();
		delay(1000);
	}
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
	DEBUG_PRINTLN("HTTPUpdateServer ready! Open http://" + String(_localWebHost) + String(_updateWebPath) + " in your browser and login with username " + String(_updateWebUsername) + " and password " + String(_updateWebPassword) + "\n");
}

void handle_root() {
	DEBUG_PRINTLN("Serving root page");
	_webClientReturnString = "Hello from Industrial IoT, read individual values from /temperature /humidity /pressure \n\n";
	//_webClientReturnString += "Humidity: " + String((int)_humidity) + "%" + " ----  Temperature: " + String((int)_temperature) + "C" + " ---- Pressure: " + String((int)_pressure) + "mb";
	_webClientReturnString += "Temperature: " + String(_currentSensorValues.Temperature) + "C" + " ---- Pressure: " + String(_currentSensorValues.Pressure) + "mb\n\n";

	_webClientReturnString += "Alert mobile number:	" + String(_currentConfigValues.SMS) + "\n";
	_webClientReturnString += "Temperature trigger (high):	" + String(_currentConfigValues.TemperatureHigh) + "\n";
	_webClientReturnString += "Temperature trigger (low):	" + String(_currentConfigValues.TemperatureLow) + "\n";
	_webClientReturnString += "Humidity trigger(high):		" + String(_currentConfigValues.HumidityHigh) + "\n";
	_webClientReturnString += "Humidity trigger(low):		" + String(_currentConfigValues.HumidityLow) + "\n";
	_webClientReturnString += "Off when value returns to (" + String(_currentConfigValues.ReturnToOffPercentage) + "%) of trigger.\n";
	_webClientReturnString += "Run every (" + String(_currentConfigValues.CheckBoundsTimerMinutes) + ") minutes.\n";
	_webClientReturnString += "\n";
	_webClientReturnString += "(" + String(_previousSensorValuesCount) + ") values in history.\n";

	_httpServer.send(200, "text/plain", _webClientReturnString);
	delay(100);
}

void setupWebServer() {
	_httpServer.on("/", &handle_root);
	_httpServer.on("/graphTemperature.svg", drawGraph);

	_httpServer.on("/temperature", []() {  // if you add this subdirectory to your webserver call, you get text below :)
		_webClientReturnString = "Temperature: " + String(_currentSensorValues.Temperature) + "C";   // Arduino has a hard time with float to string
		_httpServer.send(200, "text/plain", _webClientReturnString);            // send to someones browser when asked
	});

	_httpServer.on("/humidity", []() {  // if you add this subdirectory to your webserver call, you get text below :)
		_webClientReturnString = "Humidity: " + String(_currentSensorValues.Humidity) + "%";
		_httpServer.send(200, "text/plain", _webClientReturnString);               // send to someones browser when asked
	});

	_httpServer.on("/pressure", []() {  // if you add this subdirectory to your webserver call, you get text below :)
		_webClientReturnString = "Relative Pressure: " + String(_currentSensorValues.RelativePressure) + "mb";
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
	char __sensorStatus;

	__sensorStatus = _bmp180Sensor.startTemperature();
	if (__sensorStatus != 0)
	{
		// Wait for the measurement to complete:
		delay(__sensorStatus);

		// Retrieve the completed temperature measurement:
		// Note that the measurement is stored in the variable T.
		// Function returns 1 if successful, 0 if failure.

		//_sensorStatus = _bmp180Sensor.gethumidity;

		sensorValueStruct __newSensorValues;

		__sensorStatus = _bmp180Sensor.getTemperature(__newSensorValues.Temperature);
		if (__sensorStatus != 0)
		{
			// Print out the measurement:
			DEBUG_PRINT("temperature: ");
			DEBUG_PRINTDEC(__newSensorValues.Temperature, 2);
			DEBUG_PRINT(" deg C, ");
			DEBUG_PRINTDEC((9.0 / 5.0)*__newSensorValues.Temperature + 32.0, 2);
			DEBUG_PRINTLN(" deg F");

			// Start a pressure measurement:
			// The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
			// If request is successful, the number of ms to wait is returned.
			// If request is unsuccessful, 0 is returned.

			__sensorStatus = _bmp180Sensor.startPressure(3);
			if (__sensorStatus != 0)
			{
				// Wait for the measurement to complete:
				delay(__sensorStatus);

				// Retrieve the completed pressure measurement:
				// Note that the measurement is stored in the variable P.
				// Note also that the function requires the previous temperature measurement (T).
				// (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
				// Function returns 1 if successful, 0 if failure.

				__sensorStatus = _bmp180Sensor.getPressure(__newSensorValues.Pressure, __newSensorValues.Temperature);
				if (__sensorStatus != 0)
				{
					// Print out the measurement:
					DEBUG_PRINT("absolute pressure: ");
					DEBUG_PRINTDEC(__newSensorValues.Pressure, 2);
					DEBUG_PRINT(" mb, ");
					DEBUG_PRINTDEC(__newSensorValues.Pressure*0.0295333727, 2);
					DEBUG_PRINTLN(" inHg");

					// The pressure sensor returns abolute pressure, which varies with altitude.
					// To remove the effects of altitude, use the sealevel function and your current altitude.
					// This number is commonly used in weather reports.
					// Parameters: P = absolute pressure in mb, ALTITUDE = current altitude in m.
					// Result: p0 = sea-level compensated pressure in mb

					__newSensorValues.RelativePressure = _bmp180Sensor.sealevel(__newSensorValues.Pressure, ALTITUDE); // we're at 1655 meters (Boulder, CO)
					DEBUG_PRINT("relative (sea-level) pressure: ");
					DEBUG_PRINTDEC(__newSensorValues.RelativePressure, 2);
					DEBUG_PRINT(" mb, ");
					DEBUG_PRINTDEC(__newSensorValues.RelativePressure*0.0295333727, 2);
					DEBUG_PRINTLN(" inHg");

					// On the other hand, if you want to determine your altitude from the pressure reading,
					// use the altitude function along with a baseline pressure (sea-level or other).
					// Parameters: P = absolute pressure in mb, p0 = baseline pressure in mb.
					// Result: a = altitude in m.

					__newSensorValues.Altitude = _bmp180Sensor.altitude(__newSensorValues.Pressure, __newSensorValues.RelativePressure);
					DEBUG_PRINT("computed altitude: ");
					DEBUG_PRINTDEC(__newSensorValues.Altitude, 0);
					DEBUG_PRINT(" meters, ");
					DEBUG_PRINTDEC(__newSensorValues.Altitude*3.28084, 0);
					DEBUG_PRINTLN(" feet");
						
					_previousSensorValuesCount++;
					DEBUG_PRINT(_previousSensorValuesCount); DEBUG_PRINTLN(" Historical sensor values logged");
					if (_previousSensorValuesCount >= SENSORVALUESWIDTH) {
						//shift everything to the left
						for (int k = 0; k< SENSORVALUESWIDTH - 2; k++) {
							_previousSensorValues[k] = _previousSensorValues[k + 1];
						}
						_previousSensorValuesCount = SENSORVALUESWIDTH - 1;
					}
					_previousSensorValues[_previousSensorValuesCount] = _currentSensorValues;

					_currentSensorValues = __newSensorValues;

				}
				else DEBUG_PRINTLN("error retrieving pressure measurement\n");
			}
			else DEBUG_PRINTLN("error starting pressure measurement\n");
		}
		else DEBUG_PRINTLN("error retrieving temperature measurement\n");
	}
	else DEBUG_PRINTLN("error starting temperature measurement\n");
	
}

void checkSensorValues() {
	updateSensorValues();  //first call a value
}

void setup(void) {

	Serial.begin(115200);
	DEBUG_PRINTLN();
	DEBUG_PRINTLN("Booting IIot Sketch...");

	DEBUG_PRINTLN("Breathing LED setup");
	pinMode(LED_BUILTIN, OUTPUT);
	_tickerOnBoardLED.attach(0.2, tickLED);

	pinMode(RESETBUTTONPIN, INPUT);
	attachInterrupt(RESETBUTTONPIN, debounceInterrupt, RISING);


	setupWifi(); //blocking function

	setupBMP180();
	updateSensorValues(); //run once right away

	if (_currentConfigValues.CheckBoundsTimerMinutes < 1) { _currentConfigValues.CheckBoundsTimerMinutes = 1; } //minimum trigger is ever minute
	_sensorTimer.setInterval(_currentConfigValues.CheckBoundsTimerMinutes * 60000, checkSensorValues);

	setupHTTPUpdateServer();
	setupWebServer();

	_httpServer.begin();

	_tickerOnBoardLED.attach(1, tickLED);
}

void loop(void) {
	if (_isConnected)
	{
		_httpServer.handleClient();
		_sensorTimer.run();
	}

}
