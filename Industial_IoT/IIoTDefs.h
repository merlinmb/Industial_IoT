// IIoTDefs.h

#ifndef _IIOTDEFS_h
#define _IIOTDEFS_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

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
	double Temperature, HeatIndex, Humidity, Pressure, SeaLevelPressure, RelativePressure, Altitude, CO;
};

#endif

