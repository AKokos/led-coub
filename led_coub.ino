#include "Arduino.h"
#include <SPI.h>
//#include "GyverTimer.h"

// constants for CUSTOMIZE
#define COUB_SIZE 3 // max 16
#define BIT_DEPTH 8 // bit depth of 74HC595
#define DATA_REGISTERS_NUM 3 // used 74HC595 for voxels address

// analog pins
#define BRIGHT_CONTROL_IN_PIN 5

// digital pins
#define LATCH_PIN 10 // (PIN_SPI_SS) set pin 12 of 74HC595 as output latch RCK
#define DATA_PIN  11 // (PIN_SPI_MOSI) set pin 14 of 74HC595 as data input pin SI
#define CLOCK_PIN 13 // (PIN_SPI_SCK) set pin 11 of 74HC595 as clock pin SCK
#define BRIGHT_CONTROL_OUT_PIN 5 // set pin 13 (OE) of 74HC595 for control brightness

// service constants
#define COLOR1 0
#define COLOR2 1
#define COLOR3 2 // COLOR1+COLOR2
#define MCOLOR	 // multi-color (random of COLOR1/COLOR2/COLOR3)
#define SWITCH_LAYER_DELAY 150

// service vars
unsigned int brightnessLevel;
bool coubIsBig;
unsigned int coubs[2][COUB_SIZE][COUB_SIZE]; // array for color ( coubs[color][lay][row] =  columns in row )
unsigned int voxelsNum;
byte currentColor = COLOR1;

void setup() {
//	Serial.begin(9600);

	// set "constants"
	coubIsBig = COUB_SIZE > 8;
	voxelsNum = COUB_SIZE * COUB_SIZE * COUB_SIZE;

	// prepare pins
	pinMode(LATCH_PIN, OUTPUT);
	SPI.begin();
	SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

	//////
	digitalWrite(LATCH_PIN, LOW);
	// layers
	SPI.transfer(0x01);
	// diods
	SPI.transfer(0x00);
	SPI.transfer(0x00);
	SPI.transfer(0xa0);
	digitalWrite(LATCH_PIN, HIGH);


	/////
	coubs[COLOR1][0][0] = 0x01;
	coubs[COLOR1][0][1] = 0x01;
	coubs[COLOR1][0][2] = 0x01;

	coubs[COLOR1][1][0] = 0x02;
	coubs[COLOR1][1][1] = 0x02;
	coubs[COLOR1][1][2] = 0x02;

	coubs[COLOR1][2][0] = 0x04;
	coubs[COLOR1][2][1] = 0x04;
	coubs[COLOR1][2][2] = 0x04;

	coubs[COLOR2][0][0] = 0x04;
	coubs[COLOR2][0][1] = 0x04;
	coubs[COLOR2][0][2] = 0x04;

	coubs[COLOR2][1][0] = 0x02;
	coubs[COLOR2][1][1] = 0x02;
	coubs[COLOR2][1][2] = 0x02;

	coubs[COLOR2][2][0] = 0x01;
	coubs[COLOR2][2][1] = 0x01;
	coubs[COLOR2][2][2] = 0x01;

}


void loop() {
	// brightness control
	brightnessLevel = map(analogRead(BRIGHT_CONTROL_IN_PIN), 0, 1023, 0, 255);
	analogWrite(BRIGHT_CONTROL_OUT_PIN, brightnessLevel);

	// mmmmmmmm testing
	renderCube();

}

void renderCube() {
	// TODO: invert value if needed (check gyver's variant)
	short currentValue;
	short transRegValue = 0x00;
	short bits[COUB_SIZE];
	byte regBitIndex = 0;
	short dataRegValues[DATA_REGISTERS_NUM];
	byte currentRegisterIndex;
	short currentRegisterValue;

	for (byte lay = 0; lay < COUB_SIZE; lay++) {
		digitalWrite(LATCH_PIN, LOW);
		// choose layer
		transRegValue = 0x01 << lay;
		if (coubIsBig) {
			// has 2 registers for layers
			SPI.transfer(transRegValue >> 8);
			SPI.transfer(transRegValue & 0x0f);
		} else {
			SPI.transfer(transRegValue);
		}

		// render layer
		currentRegisterIndex = DATA_REGISTERS_NUM - 1;
		currentRegisterValue = 0x00;
		for (byte coub = 0; coub < 2; coub++) {
			for (byte row = 0; row < COUB_SIZE; row++) {
				currentValue = coubs[coub][lay][row];

				// render columns in row
				// - fill operative bits array with value bits
				for (byte i = 0; i < COUB_SIZE; i++) {
					bits[i] = (currentValue & 0x01) << BIT_DEPTH - 1;
					currentValue >>= 1;
				}
				// - fill value for register
				for (byte i = 0; i < COUB_SIZE; i++) {
					// add bit to reg value
					currentRegisterValue |= bits[i];
					regBitIndex++;
					if (regBitIndex < BIT_DEPTH) {
						currentRegisterValue >>= 1;
					} else {
						dataRegValues[currentRegisterIndex] =
								currentRegisterValue;
						currentRegisterIndex--;
						currentRegisterValue = 0x00;
						regBitIndex = 0;
					}
				}
			}
		}
		// shift bits if last register is not full
		if (regBitIndex > 0 && regBitIndex < BIT_DEPTH) {
			currentRegisterValue >>= BIT_DEPTH - regBitIndex - 1;
			dataRegValues[currentRegisterIndex] = currentRegisterValue;
			currentRegisterIndex--;
		}
		regBitIndex = 0;
		for (byte i = 0; i < DATA_REGISTERS_NUM; i++) {
			SPI.transfer(dataRegValues[i]);
		}
		digitalWrite(LATCH_PIN, HIGH);
		delayMicroseconds(SWITCH_LAYER_DELAY);
	}

	// вырубаем всё
	digitalWrite(LATCH_PIN, LOW);
	SPI.transfer(0x00);
	digitalWrite(LATCH_PIN, HIGH);
}

/*void printBits(char prefix[], byte myByte, short delim, boolean newLine) {
	Serial.print(prefix);
	byte ind = 0;
	for (byte mask = 0x80; mask; mask >>= 1) {
		if (mask & myByte) {
			Serial.print('1');
		} else {
			Serial.print('0');
		}
		ind++;
		if (ind == delim) {
			ind = 0;
			Serial.print(' ');
		}
	}
	if (newLine) {
		Serial.println();
	}
 }*/
