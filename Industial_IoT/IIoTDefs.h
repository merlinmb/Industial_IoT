#pragma once
// IIoTDefs.h

#ifndef _IIOTDEFS_h
#define _IIOTDEFS_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

struct configValues {
	char SMS[13] = "+27834250558";		// Alert mobile number
	int TemperatureHigh = 25;		// Temperature trigger (high)
	int TemperatureLow = 4;		// Temperature trigger (low)
	int HumidityHigh = 80;		// Humidity trigger (high)
	int HumidityLow = 9;		// Humidity trigger (low)
	int ReturnToOffPercentage = 10;	// Off when value returns to % of trigger
	int CheckBoundsTimerMinutes = 3;	// default trigger is ever minute
};


struct sensorValueStruct {
	unsigned long EpochSeconds;	//number of seconds since the epoch, usage: DateTime dt = RTC.now(); time = dt.unixtime();
	double Temperature, HeatIndex, Humidity, Pressure, SeaLevelPressure, RelativePressure, Altitude, COppm;
};

#define ALTITUDE 1555.0 // Altitude of SparkFun's HQ in Boulder, CO. in meters

const long SIM800LTIMEUPDATEFREQ = 600000;    // the pin to send the wakeup sequence to for the Sim800 GSm device

String _localWebHost = "IIot";
const char* _updateWebPath = "/firmware";
const char* _updateWebUsername = "admin";
const char* _updateWebPassword = "admin";

configValues _currentConfigValues;
sensorValueStruct _currentSensorValues;
const int SENSORVALUESWIDTH = 50;
int _previousSensorValuesCount = 0;
sensorValueStruct _previousSensorValues[SENSORVALUESWIDTH];

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

#define DHTTYPE DHT11		// DHT 11
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)
DHT _dht(DHTPIN, DHTTYPE);
#define DHTRETRIES 3  //number of retries for reading from the DHT sensor

Sim800l _sim800l;


boolean _MQHeaterHigh = true;
byte _MQHeatCycles = 0;

//  60 sec high heat (5.0V)
unsigned long _MQTimerA = 60000;
unsigned long _MQTimerAlap = millis();  // timer

										//  90 sec low heat (1.4V)
unsigned long _MQTimerB = 90000;
unsigned long _MQTimerBlap = millis();  // timer
									 //  The difference between timerB and timerRead is 
									 //  how long a measurement will be made for MQ-7.
unsigned long _MQTimerRead = 60000;

boolean _MQAlarmCO = false;
//  The USA OSHA exposure limit for CO is 50 ppm.
//  The average level in a home is 0.5 to 5 ppm.
//  The level in a home with a proper adj gas stove is 5 to 15 ppm.
//  A CO of 667 ppm may result in seizure, coma, and death.
const unsigned int _MQCOThreshold = 50;
// Adjust vRef to be the true supply voltage in mV.
float _MQVoltageRef = 4364.0;
float _MQRL = 10.0;  //  load resistor value in k ohms
float _MQRo = 10.0;  //  default value 10 k ohms.  Revised during calibration.
const float _MQRo_clean_air_factor = 10.0;

float _MQmV = 0.0;
unsigned long _MQSamples = 0;

float RsRoAtAmbientTo20C65RH(float RsRo_atAmb, float ambTemp, float ambRH) {
	//  Using the datasheet for MQ-7 sensor, derive Rs/Ro values 
	//  from - 10 to 50 C and 33, 65, and 85 % relative humidity.
	//  For the measured Rs/Ro, use linear interpolation to calculate the
	//  standard Rs/Ro values for the measured ambient temperature and RH.
	//  Next, calculate a correction factor from the standard Rs/Ro at ambient
	//  temp and RH relative to standard Rs/Ro at 20C and 65 RH.  
	//  Apply this correction factor to the measured Rs/Ro value and return the
	//  corrected value.  This corrected value may then be used against the Rs/Ro
	//  Rs/Ro vs CO concentration (ppm) chart to estimate the concentration of CO.

	//  Calc RsRo values at ambTemp & 33% RH, 65% and 85% RH
	float RsRo_at_ambTemp_33RH = -0.00000593 * pow(ambTemp, 3) + 0.000533 * pow(ambTemp, 2) - 0.0182 * ambTemp + 1.20;
	float RsRo_at_ambTemp_85RH = -0.0000000741 * pow(ambTemp, 3) + 0.000114 * pow(ambTemp, 2) - 0.0114 * ambTemp + 1.03;
	//float RsRo_at_65RH = ((65.0-33.0)/(85.0-65.0));
	float RsRo_at_ambTemp_65RH = ((65.0 - 33.0) / (85.0 - 33.0)*(RsRo_at_ambTemp_85RH - RsRo_at_ambTemp_33RH) + RsRo_at_ambTemp_33RH)*1.102;
	//  Linear interpolate to get the RsRo at the ambient RH value (ambRH).
	float RsRo_at_ambTemp_ambRH;
	if (ambRH < 65.0) {
		RsRo_at_ambTemp_ambRH = (ambRH - 33.0) / (65.0 - 33.0)*(RsRo_at_ambTemp_65RH - RsRo_at_ambTemp_33RH) + RsRo_at_ambTemp_33RH;
	}
	else {
		// ambRH > 65.0
		RsRo_at_ambTemp_ambRH = (ambRH - 65.0) / (85.0 - 65.0)*(RsRo_at_ambTemp_85RH - RsRo_at_ambTemp_65RH) + RsRo_at_ambTemp_65RH;
	}
	//  Calc the correction factor to bring RsRo at ambient temp & RH to 20 C and 65% RH.
	const float refRsRo_at_20C65RH = 1.00;
	float RsRoCorrPct = 1 + (refRsRo_at_20C65RH - RsRo_at_ambTemp_ambRH) / refRsRo_at_20C65RH;
	//  Calculate what the measured RsRo at ambient conditions would be corrected to the
	//  conditions for 20 C and 65% RH.
	float measured_RsRo_at_20C65RH = RsRoCorrPct * RsRo_atAmb;
	return measured_RsRo_at_20C65RH;
}

float CalcRsFromVo(float Vo) {
	//  Vo = sensor output voltage in mV.
	//  VRef = supply voltage, 5000 mV
	//  RL = load resistor in k ohms
	//  The equation Rs = (Vc - Vo)*(RL/Vo)
	//  is derived from the voltage divider
	//  principle:  Vo = RL * Vc (Rs + RL)
	//
	//  Note.  Alternatively you could calc
	//         Rs from ADC value using
	//         Rs = RL * (1024 - ADC) / ADC
	float Rs = (_MQVoltageRef - Vo) * (_MQRL / Vo);
	return Rs;
}

unsigned int GetCOPpmForRatioRsRo(float RsRo_ratio) {
	//  If you extract the data points from the CO concentration
	//  versus Rs/Ro chart in the datasheet, plot the points,
	//  fit a polynomial curve to the points, you come up with the equation
	//  for the curve of:  Rs/Ro = 22.073 * (CO ppm) ^ -0.66659
	//  This equation is valid for ambient conditions of 20 C and 65% RH.
	//  Solving for the concentration of CO you get:
	//    CO ppm = [(Rs/Ro)/22.073]^(1/-0.66666)
	float ppm;
	ppm = pow((RsRo_ratio / 22.073), (1 / -0.66659));
	return (unsigned int)ppm;
}

float Get_mVfromADC(byte AnalogPin) {
	// read the value from the sensor:
	int ADCval = analogRead(AnalogPin);
	// It takes about 100 microseconds (0.0001 s) to read an analog input
	delay(1);
	//  Voltage at pin in milliVolts = (reading from ADC) * (5000/1024) 
	float mV = ADCval * (_MQVoltageRef / 1024.0);
	return mV;
}

void blinkLED(byte ledPIN) {
	//  consumes 300 ms.
	for (int i = 5; i > 0; i--) {
		digitalWrite(ledPIN, HIGH);
		delay(30);
		digitalWrite(ledPIN, LOW);
		delay(30);
	}
}

void readDHTSensor(sensorValueStruct *newValueStruct) {

	double __hVal, __tVal;

	for (int i = 0; i < DHTRETRIES; i++) {
		// Reading temperature or humidity takes about 250 milliseconds!
		// Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
		__hVal = _dht.readHumidity();
		delay(250);
		__tVal = _dht.readTemperature(); // Read temperature as Celsius (the default)
										 // Check if any reads failed and exit early (to try again).
		if (isnan(__hVal) || isnan(__tVal)) {
			DEBUG_PRINTLN("Failed to read from DHT sensor!");
			delay(1000);
		}
		else {
			break;

		}
	}
	newValueStruct->Humidity = __hVal;
	newValueStruct->Temperature = __tVal;
	//__newSensorValues.Temperature = _dht.readTemperature(true); // Read temperature as Fahrenheit (isFahrenheit = true)
	// Compute heat index in Celsius (isFahreheit = false)
	newValueStruct->HeatIndex = _dht.computeHeatIndex(newValueStruct->Temperature, newValueStruct->Humidity, false);

}

#endif

