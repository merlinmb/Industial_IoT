#pragma once
// IIoTPinouts.h

#ifndef _IIoTPinouts_h
#define _IIoTPinouts_h

const byte _MQPin = A0;
const int SIM800_RI = 4;    // Sim800l status pin
const int SIM800_PWR = 9;    // the pin to send the wakeup sequence to for the Sim800 GSm device
#define DHTPIN 10			// SD3 (GOI10) what digital pin we're connected to
const int RESETBUTTONPIN = 13;    // the pin that the pushbutton is attached to
const byte _MQPinGreenLED = 14;
const byte _MQPinRedLED = 14;
const byte _MQPinBuzzer = 14;
const byte _MQPinNPN = 14;



#endif