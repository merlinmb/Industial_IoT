#include <FS.h>						//this needs to be first, or it all crashes and burns...

#include <ArduinoJson.h>			//https://github.com/bblanchon/ArduinoJson

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>			// https://github.com/tzapu/WiFiManager
#include <SimpleTimer.h>
#include <Ticker.h>
#include <QueueArray.h>
#include <Sim800l.h>
#include <DHT.h>

#include <SoftwareSerial.h> //is necesary for the library!! 
#include <TimeLib.h>

#include "IIoTPinouts.h" //must be above IIotDefs
#include "IIoTDefs.h"

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


time_t syncSoftwareRTC()
{
	DEBUG_PRINTLN("Sim800l: updating clock");
	int day, month, year, hour, minute, second;
	//fetch the actual time from the Sim800l
	_sim800l.RTCtime(&day, &month, &year, &hour, &minute, &second);

	tmElements_t __tm;

	if (year > 99)
		year = year - 1970;
	else
		year += 30;
	__tm.Year = year;
	__tm.Month = month;
	__tm.Day = day;
	__tm.Hour = hour;
	__tm.Minute = minute;
	__tm.Second = second;
	return makeTime(__tm);
}

void drawGraph() {
	String out = "";
	char temp[100];
	out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + String((_previousSensorValuesCount+1) * 10) + "\" height=\"500\">\n";
	out += "<rect width=\"" + String((_previousSensorValuesCount+1) *10) + "\" height=\"500\" fill=\"rgb(250, 250, 250)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
	out += "<g stroke=\"#0074d9\">\n";
	if (_previousSensorValuesCount>=2){
		for (int x = 0; x < _previousSensorValuesCount-1; x++) {
			int y = (int)(_previousSensorValues[x].Temperature*10);
			int y2 = (int)(_previousSensorValues[x+1].Temperature*10);
			sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", (x)*10, 500 - y, (x+1) * 10, 500 - y2);
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

	_localWebHost = String("IIoT_" + String(ESP.getChipId()));
	DEBUG_PRINT("Hostname: "); DEBUG_PRINTLN(_localWebHost);

	const char * __hostDNS = _localWebHost.c_str();

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
	MDNS.begin(__hostDNS);

	_isConnected = true;
}

void resetFSInterrupt() {
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
		resetFSInterrupt();
		_lastMicros = micros();
	}
}

void setupHTTPUpdateServer() {

	_httpUpdater.setup(&_httpServer, _updateWebPath, _updateWebUsername, _updateWebPassword);

	MDNS.addService("http", "tcp", 80);
	DEBUG_PRINTLN("HTTPUpdateServer ready! Open http://" + String(_localWebHost)+ ".local"+ String(_updateWebPath) + " in your browser and login with username " + String(_updateWebUsername) + " and password " + String(_updateWebPassword) + "\n");
}

void handle_root() {
	DEBUG_PRINTLN("Serving root page");
	_webClientReturnString = "Hello from Industrial IoT, read individual values from /temperature /humidity /pressure \n\n";
	_webClientReturnString += "Temperature: " + String(_currentSensorValues.Temperature) + "C" + " ---- Humidity: " + String(_currentSensorValues.Humidity) + "%\n\n";
	//_webClientReturnString += "Temperature: " + String(_currentSensorValues.Temperature) + "C" + " ---- Pressure: " + String(_currentSensorValues.Pressure) + "mb\n\n";

	_webClientReturnString += "Alert mobile number:	" + String(_currentConfigValues.SMS) + "\n";
	_webClientReturnString += "Temperature trigger (high):	" + String(_currentConfigValues.TemperatureHigh) + "\n";
	_webClientReturnString += "Temperature trigger (low):	" + String(_currentConfigValues.TemperatureLow) + "\n";
	_webClientReturnString += "Humidity trigger (high):		" + String(_currentConfigValues.HumidityHigh) + "\n";
	_webClientReturnString += "Humidity trigger (low):		" + String(_currentConfigValues.HumidityLow) + "\n";
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

void printSensorValues(sensorValueStruct *SensorValues)
{
	DEBUG_PRINT("Capture Date & Time: ");
	time_t __captureTime = time_t(SensorValues->EpochSeconds);
	char __captureTimeStr[28];
	sprintf(__captureTimeStr, "%02d/%02d/%02d %02d:%02d:%02d", year(__captureTime), month(__captureTime), day(__captureTime), hour(__captureTime), minute(__captureTime), second(__captureTime));


	DEBUG_PRINTLN(__captureTimeStr);
	DEBUG_PRINT("Temperature: ");
	DEBUG_PRINTDEC(SensorValues->Temperature, 2);
	DEBUG_PRINT(" deg C, ");
	DEBUG_PRINTDEC((9.0 / 5.0)*SensorValues->Temperature + 32.0, 2);
	DEBUG_PRINTLN(" deg F");
	DEBUG_PRINT("Feels like: ");
	DEBUG_PRINTDEC(SensorValues->HeatIndex, 2);
	DEBUG_PRINTLN(" deg C");
	DEBUG_PRINT("Humidity: ");
	DEBUG_PRINTDEC(SensorValues->Humidity, 2);
	DEBUG_PRINTLN(" %");
	DEBUG_PRINT("CO levels: ");
	DEBUG_PRINTDEC(SensorValues->COppm, 2);
	DEBUG_PRINTLN(" ppm");
	/*
	DEBUG_PRINT("absolute pressure: ");
	DEBUG_PRINTDEC(SensorValues->Pressure, 2);
	DEBUG_PRINT(" mb, ");
	DEBUG_PRINTDEC(SensorValues->Pressure*0.0295333727, 2);
	DEBUG_PRINTLN(" inHg");
	DEBUG_PRINT("relative (sea-level) pressure: ");
	DEBUG_PRINTDEC(SensorValues->RelativePressure, 2);
	DEBUG_PRINT(" mb, ");
	DEBUG_PRINTDEC(SensorValues->RelativePressure*0.0295333727, 2);
	DEBUG_PRINTLN(" inHg");
	DEBUG_PRINT("computed altitude: ");
	DEBUG_PRINTDEC(SensorValues->Altitude, 0);
	DEBUG_PRINT(" meters, ");
	DEBUG_PRINTDEC(SensorValues->Altitude*3.28084, 0);
	DEBUG_PRINTLN(" feet");
	*/
}

unsigned int updateMQValues()
{
	unsigned int COVal = 0;
	// if millis() or timer wraps around, we'll just reset it
	if (_MQTimerAlap > millis())  _MQTimerAlap = millis();
	if (_MQTimerBlap > millis())  _MQTimerBlap = millis();

	if (_MQHeaterHigh == false && _MQHeatCycles == 2 && (millis() - _MQTimerBlap > _MQTimerRead)) {
		//  take reading of MQ sensor..
		digitalWrite(_MQPinGreenLED, HIGH);
		_MQmV += Get_mVfromADC(_MQPin);
		_MQSamples += 1;
	}
	else {
		digitalWrite(_MQPinGreenLED, LOW);
	}
	
	if (_MQHeaterHigh == true) {
		//  High heat applied for 60 sec
		digitalWrite(_MQPinNPN, HIGH);
		//  Timer A
		if (millis() - _MQTimerAlap > _MQTimerA) {
			_MQTimerAlap = millis(); // reset the timer
			_MQTimerBlap = millis(); // reset the timer
			_MQHeaterHigh = false;
		}
	}
	else {
		//  _MQHeaterHigh = false
		//  Low heat applied for 90 sec
		digitalWrite(_MQPinNPN, LOW);
		//  Timer B
		if (millis() - _MQTimerBlap > _MQTimerB) {
			_MQTimerAlap = millis(); // reset the timer
			_MQTimerBlap = millis(); // reset the timer
			_MQHeaterHigh = true;
			_MQHeatCycles += 1;
			DEBUG_PRINT("end of heat_cycle = ");
			DEBUG_PRINTLN(_MQHeatCycles);
			//  Report on MQ-7 measurement at end of 
			//  the low phase of the 3rd heat cycle.
			if (_MQHeatCycles == 3) {
				_MQmV = _MQmV / float(_MQSamples);
				DEBUG_PRINT("samples = ");
				DEBUG_PRINTLN(_MQSamples);
				DEBUG_PRINT("A"); DEBUG_PRINT(_MQPin); DEBUG_PRINT("  = "); DEBUG_PRINT(_MQmV); DEBUG_PRINTLN(" mV");
				float RsVal = CalcRsFromVo(_MQmV);
				DEBUG_PRINT("Rs = ");  DEBUG_PRINTLN(RsVal);
				_MQmV = 0.0;
				_MQSamples = 0;
				COVal = GetCOPpmForRatioRsRo((RsVal / _MQRo));
				DEBUG_PRINT("CO = "); DEBUG_PRINT(COVal); DEBUG_PRINTLN(" ppm");
				//Blynk.virtualWrite(V2, COVal);
				//Blynk.virtualWrite(V3, RsVal);		
			}
		}
	}

	if (_MQHeatCycles >= 3) {
		_MQHeatCycles = 0;
	}
	return COVal;
}

void updateSensorValues() {

	sensorValueStruct __newSensorValues;
	__newSensorValues.EpochSeconds = (unsigned long)(now());

	readDHTSensor(&__newSensorValues);

	__newSensorValues.COppm = updateMQValues();


	if (_previousSensorValuesCount >= SENSORVALUESWIDTH) {
		DEBUG_PRINTLN("Shift everything to the left");
		for (int k = 0; k< SENSORVALUESWIDTH - 2; k++) {
			_previousSensorValues[k] = _previousSensorValues[k + 1];
		}
		_previousSensorValuesCount = SENSORVALUESWIDTH - 1;
	}
	DEBUG_PRINT("Updating historical sensor value: "); DEBUG_PRINTLN(_previousSensorValuesCount);
	_previousSensorValues[_previousSensorValuesCount] = ((_previousSensorValuesCount == 0)?__newSensorValues: _currentSensorValues);
	DEBUG_PRINTLN(""); DEBUG_PRINT(_previousSensorValuesCount+1); DEBUG_PRINTLN(" historical sensor values now logged");


	if (_previousSensorValuesCount > 0) {
		DEBUG_PRINT("Previous sensor value: "); DEBUG_PRINTLN(_previousSensorValuesCount);
		printSensorValues(&_previousSensorValues[_previousSensorValuesCount]);
	}

	_currentSensorValues = __newSensorValues;
	_previousSensorValuesCount++;

	DEBUG_PRINTLN("\nCurrent sensor values:");
	printSensorValues(&_currentSensorValues);

	
}

void checkSensorValues() {
	updateSensorValues();  //first call a value
}

/*
* Name: PowerUp
* Description: Soft power up
*/
void powerUpSim800l()
{
	DEBUG_PRINT("Set RI pin to input, reading: ");
	pinMode(SIM800_RI, INPUT);  //check if the Sim800 is awake
	delay(10);
	if (digitalRead(SIM800_RI) == LOW) {
		DEBUG_PRINTLN(digitalRead(SIM800_RI));

		DEBUG_PRINTLN("Sim800 is in sleep mode, powering up... Stage 1");
		pinMode(SIM800_PWR, OUTPUT);
		delay(100);
		DEBUG_PRINTLN("Sim800 powering up... Stage 2");
		digitalWrite(SIM800_PWR, LOW);
		delay(1000);
		DEBUG_PRINTLN("Sim800 powering up... Stage 3");
		digitalWrite(SIM800_PWR, HIGH);
		delay(5000);
		DEBUG_PRINTLN("Done check green light...");
	}
	else
	{ 
		DEBUG_PRINTLN("\nSim800 is already active (on)");
	}
}

void setupSim800l()
{
	powerUpSim800l(); //if off (or in sleep mode)	

	DEBUG_PRINTLN("\nSim800 initialization");
	_sim800l.begin(); // initializate the library. 

	DEBUG_PRINTLN("Read the first SMS");
	String text = _sim800l.readSms(1); // first position in the prefered memory storage. 
	DEBUG_PRINTLN(text);
	DEBUG_PRINT("Signal Quality, result: ");	
	String sq = _sim800l.signalQuality();		DEBUG_PRINTLN(sq);
	DEBUG_PRINT("Update Timezone, result: ");
	bool __result = _sim800l.updateRtc(2); 		DEBUG_PRINTLN(__result);
	String __resultStr = _sim800l.dateNet();
	DEBUG_PRINT("Fetch Time, result: ");	DEBUG_PRINTLN(__resultStr);
	//Sim800l.callNumber("*130*601#");
	//Sim800l.delAllSms();

	setSyncProvider(syncSoftwareRTC);
	setSyncInterval(SIM800LTIMEUPDATEFREQ);		//sync the time every # seconds
}

void setupMQSensor(){
	pinMode(_MQPinGreenLED, OUTPUT);
	delay(1);
	pinMode(_MQPinRedLED, OUTPUT);
	delay(1);
	pinMode(_MQPin, INPUT);
	delay(1);
	pinMode(_MQPinBuzzer, OUTPUT);
	delay(1);
	pinMode(_MQPinNPN, OUTPUT);
	delay(1);


	DEBUG_PRINTLN("Calibrating MQ-7 CO sensor in clean air..");
	DEBUG_PRINTLN("  60 sec high heat cycle..");
	digitalWrite(_MQPinNPN, HIGH);
	// set = 200
	for (int i = 200; i>0; i--) {
		blinkLED(_MQPinGreenLED);
	}
	digitalWrite(_MQPinNPN, LOW);
	DEBUG_PRINTLN("  60 sec warmup complete");

	DEBUG_PRINTLN("  90 sec heat cycle..");
	// set = 300
	for (int i = 300; i>0; i--) {
		blinkLED(_MQPinGreenLED);
	}
	DEBUG_PRINTLN("  90 sec warmup complete.  Reading MQ-7..");

	// If mV > 3000, then repeat warm-up..

	//  take a reading..
	// set = 300
	for (int i = 300; i>0; i--) {
		blinkLED(_MQPinGreenLED);
		_MQmV += Get_mVfromADC(_MQPin);
		_MQSamples += 1;
	}
	_MQmV = _MQmV / (float)_MQSamples;
	Serial.print("  avg A");
	DEBUG_PRINT(_MQPin);
	DEBUG_PRINT(" for ");
	DEBUG_PRINT(_MQSamples);
	DEBUG_PRINT(" samples = ");
	DEBUG_PRINT(_MQmV);
	DEBUG_PRINTLN(" mV");
	DEBUG_PRINT("  Rs = ");
	DEBUG_PRINTLN(CalcRsFromVo(_MQmV));
	//  Conv output to Ro
	//  Ro = calibration factor for measurement in clean air.
	//  Ro = ((vRef - mV) * RL) / (mV * Ro_clean_air_factor);
	//  Hereafter, measure the sensor output, convert to Rs, and
	//  then calculate Rs/Ro using: Rs = ((Vc-Vo)*RL) / Vo
	_MQRo = CalcRsFromVo(_MQmV) / _MQRo_clean_air_factor;
	DEBUG_PRINT("  Ro = ");
	DEBUG_PRINTLN(_MQRo);
	//  Values in clean air are:
	//    Rs = 6.99
	//    Ro = 0.70
	DEBUG_PRINTLN("Sensor calibration in clean air complete");
	DEBUG_PRINTLN("Setup complete.  Monitoring for CO..");
	DEBUG_PRINTLN("  ");
	digitalWrite(_MQPinNPN, LOW);
	_MQmV = 0.0;
	_MQSamples = 0;

	//  Start with heater on high
	_MQHeaterHigh = true;
	_MQTimerAlap = millis(); // reset the timer

	blinkLED(_MQPinGreenLED);
	blinkLED(_MQPinRedLED);
	//  Chirp the buzzer
	digitalWrite(_MQPinBuzzer, HIGH);
	delay(1);
	digitalWrite(_MQPinBuzzer, LOW);
	delay(1);
	//digitalWrite(_MQPinGreenLED, HIGH);
	//delay(1);
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

	_dht.begin(); //start the temperature/ humidity sensor
	setupMQSensor();


	setupSim800l();		//start the GSM device

	updateSensorValues(); //run once right away
	if (_currentConfigValues.CheckBoundsTimerMinutes < 3) { _currentConfigValues.CheckBoundsTimerMinutes = 1; } //minimum trigger is ever 3 minutes
	_sensorTimer.setInterval(_currentConfigValues.CheckBoundsTimerMinutes * 60000, checkSensorValues);  //change to 60000 in production

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
