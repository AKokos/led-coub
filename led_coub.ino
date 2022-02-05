#include "Arduino.h"
#include <SPI.h>
#include "GyverButton.h"
//#include "GyverTimer.h"

// constants for CUSTOMIZE
#define CUBE_SIZE 3 // max 16
#define BIT_DEPTH 8 // bit depth of 74HC595
#define DATA_REGISTERS_NUM 3 // used 74HC595 for voxels address

// analog pins
#define BRIGHT_CONTROL_IN_PIN 5
#define J1_X_PIN 0
#define J1_Y_PIN 1
#define J2_X_PIN 0
#define J2_Y_PIN 1

// digital pins
#define LATCH_PIN 10 // (PIN_SPI_SS) set pin 12 of 74HC595 as output latch RCK
#define DATA_PIN  11 // (PIN_SPI_MOSI) set pin 14 of 74HC595 as data input pin SI
#define CLOCK_PIN 13 // (PIN_SPI_SCK) set pin 11 of 74HC595 as clock pin SCK
#define BRIGHT_CONTROL_OUT_PIN 5 // set pin 13 (OE) of 74HC595 for control brightness
#define LOADING_LED 3 // usually red
#define RUNNING_LED 2 // usually green
#define J1_BTN_PIN 9
#define J2_BTN_PIN 9

// modes
#define RAIN 0
#define PLANES 1
#define FILL 2

#define LAST_MODE 2
#define DEV_MODE_ON true // single dot moved by joystick

// mode timeouts
#define RAIN_TIME 260
#define PLANES_TIME 220 *2
#define FILL_TIME 8 *20
#define DOT_TIME 300

// service constants
#define SWITCH_LAYER_DELAY 50

// colors
#define COLOR1 0
#define COLOR2 1
#define COLOR3 2 // COLOR1 + COLOR2
// - next colors not for all modes (or works differently in modes)
#define COLORM 3 // multi-color (continiously change color COLOR1/COLOR2/COLOR3)
#define COLORR 4 // random-color (random of COLOR1/COLOR2/COLOR3)

#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2

#define X_POS 0
#define X_NEG 1
#define Y_POS 2
#define Y_NEG 3
#define Z_POS 4
#define Z_NEG 5
#define INVERT_X 0
#define INVERT_Y 0
#define INVERT_Z 1

#define J_LOW 300
#define J_HIGH 800
#define J_INVERT_X 1 // invert x-axis of joysticks
#define J_INVERT_Y 0 // invert y-axis of joysticks

// service vars
byte brightnessLevel;
bool cubeIsBig;
unsigned int cubes[2][CUBE_SIZE][CUBE_SIZE]; // array for color ( cubes[color][lay][row] =  columns in row )
unsigned int voxelsNum;
unsigned int voxelsArray[CUBE_SIZE * CUBE_SIZE * CUBE_SIZE];
unsigned int voxelIndex;
byte x, y, z;

GButton j1Button(J1_BTN_PIN);
GButton j2Button(J2_BTN_PIN);

byte currentColor = COLOR1;
byte currentMultiColor = COLOR2;
byte currentMode = RAIN;
unsigned int timer = 0;
unsigned int modeTimer;
byte modeStage = 0;
bool loading = true;

void setup() {
//	Serial.begin(9600);

	// set "constants"
	cubeIsBig = CUBE_SIZE > 8;
	voxelsNum = CUBE_SIZE * CUBE_SIZE * CUBE_SIZE;

	randomSeed(analogRead(4));

	// prepare pins
	pinMode(LOADING_LED, OUTPUT);
	pinMode(RUNNING_LED, OUTPUT);
	pinMode(LATCH_PIN, OUTPUT);
	SPI.begin();
	SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

	changeMode(currentMode);
}


void loop() {
	// brightness control
	brightnessLevel = map(analogRead(BRIGHT_CONTROL_IN_PIN), 0, 1023, 0, 255);
	analogWrite(BRIGHT_CONTROL_OUT_PIN, brightnessLevel);

	if (!DEV_MODE_ON) {
		// TODO: check buttons (or joysticks)
		// change mode
		// XXX: uncomment change mode&color by joystick1 when second joystick2 will arrived
		int modeShift = getJoystickMove(X_AXIS, J1_X_PIN);
		if (modeShift != 0) {
			changeMode(currentMode + modeShift);
		}
		// change color
		int colorShift = getJoystickMove(Y_AXIS, J1_Y_PIN);
		if (colorShift != 0) {
			currentColor += colorShift;
			if (currentColor > 100) {
				currentColor = COLORR;
			} else if (currentColor > COLORR) {
				currentColor = COLOR1;
			}
			changeMode(currentMode); // reset current mode
		}
		// -- change speed
		// run current mode (change voxels state)
		// @formatter:off
		switch (currentMode) {
			case RAIN: rain(); break;
			case PLANES: planes(); break;
			case FILL: fill(); break;
			default: currentMode = RAIN;
		}
	} else {
		dot();
	}

	// @formatter:on

	render();
}

void changeMode(byte mode) {
	// TODO: cleanup and other switching things
	digitalWrite(LOADING_LED, HIGH);
	digitalWrite(RUNNING_LED, LOW);
	clear();
	loading = true;
	timer = 0;
	modeStage = 0;
	randomSeed(millis());
	if (mode > 100) { // in byte -1 = 255
		currentMode = LAST_MODE;
	} else if (mode > LAST_MODE) {
		currentMode = 0;
	} else {
		currentMode = mode;
	}
	// @formatter:off
	switch (currentMode) {
		case RAIN: modeTimer = RAIN_TIME; break;
		case PLANES: modeTimer = PLANES_TIME; break;
		case FILL: modeTimer = FILL_TIME; break;
		default: modeTimer = RAIN_TIME;
	}
	// @formatter:on
	delay(500);
}

void finishLoading() {
	digitalWrite(LOADING_LED, LOW);
	digitalWrite(RUNNING_LED, HIGH);
	loading = false;
}

///////// RAIN MODE /////////////////

void rain() {
	if (loading) {
		clear();
		finishLoading();
	}

	// check timer
	timer++;
	if (timer < modeTimer) {
		// not time to change
		return;
	}

	// draw changes
	timer = 0;
	shift(Y_NEG);
	byte numDrops = random(0, 5);
	for (byte i = 0; i < numDrops; i++) {
		setVoxel(random(0, CUBE_SIZE), CUBE_SIZE - 1, random(0, CUBE_SIZE));
	}
}

///////// PLANES MODE ///////////////

byte planeDirection;
byte planePosition;

void planes() {
	if (loading) {
		clear();
		if (currentColor == COLORM) {
			nextMultiColor();
		}
		byte axis = random(0, 3);
		planePosition = random(0, 2) * (CUBE_SIZE - 1);
		setPlane(axis, planePosition);
		// @formatter:off
		switch (axis) {
			case X_AXIS: planeDirection = X_POS; break;
			case Y_AXIS: planeDirection = Y_POS; break;
			case Z_AXIS: planeDirection = Z_POS; break;
		}
		// @formatter:on
		if (planePosition != 0) {
			planeDirection++; // inc by 1 set NEG direction
		}
		timer = 0;
		modeStage = 0;
		finishLoading();
	}

	// check timer
	timer++;
	if (timer < modeTimer) {
		// not time to change
		return;
	}

	// restart mode
	if (modeStage == 2) {
		loading = true;
		return;
	}

	// draw changes
	timer = 0;
	shift(planeDirection);
	if (planeDirection % 2 == 0) {
		planePosition++;
		if (planePosition == CUBE_SIZE - 1) {
			if (modeStage == 1) {
				modeStage++;
			} else {
				planeDirection++;
				modeStage++;
			}
		}
	} else {
		planePosition--;
		if (planePosition == 0) {
			if (modeStage == 1) {
				modeStage++;
			} else {
				planeDirection--;
				modeStage++;
			}
		}
	}
}

void setPlane(byte axis, byte position) {
	for (byte i = 0; i < CUBE_SIZE; i++) {
		for (byte j = 0; j < CUBE_SIZE; j++) {
			if (axis == X_AXIS) {
				setVoxel(position, i, j);
			} else if (axis == Y_AXIS) {
				setVoxel(i, position, j);
			} else if (axis == Z_AXIS) {
				setVoxel(i, j, position);
			}
		}
	}
}

///////// FILL MODE /////////////////

void fill() {
	if (loading) {
		clear();
		if (currentColor == COLORM) {
			nextMultiColor();
		}
		randomSeed(millis());
		// fill array with order num
		for (voxelIndex = 0; voxelIndex < voxelsNum; voxelIndex++) {
			voxelsArray[voxelIndex] = voxelIndex;
		}
		// random voxels array
		unsigned int randomIndex;
		for (voxelIndex = 0; voxelIndex < voxelsNum; voxelIndex++) {
			// switch values
			randomIndex = random(0, voxelsNum);
			if (randomIndex != voxelIndex) {
				voxelsArray[voxelIndex] += voxelsArray[randomIndex];
				voxelsArray[randomIndex] = voxelsArray[voxelIndex]
						- voxelsArray[randomIndex];
				voxelsArray[voxelIndex] -= voxelsArray[randomIndex];
			}
		}
		voxelIndex = 0;
		modeStage = 0;
		timer = 0;
		finishLoading();
	}

	// check timer
	timer++;
	if (timer < modeTimer) {
		// not time to change
		return;
	}

	// restart mode
	if (modeStage == 2) {
		loading = true;
		return;
	}

	// draw changes
	timer = 0;
	if (modeStage == 0) {
		// filling
		changeCurrentArrayVoxel(true);
		if (voxelIndex == voxelsNum - 1) {
			// filling finished
			modeStage = 1;
		} else {
			voxelIndex++;
		}
	} else {
		// filling out
		changeCurrentArrayVoxel(false);
		if (voxelIndex == 0) {
			// filling out finished - need reloading
			modeStage = 2;
		} else {
			voxelIndex--;
		}
	}
}

///////// DOT MODE //////////////////

void dot() {
	if (loading) {
		clear();
		if (currentColor == COLORM) {
			nextMultiColor();
		}
		//	randomSeed(millis());
		x = random(0, CUBE_SIZE);
		y = random(0, CUBE_SIZE);
		z = random(0, CUBE_SIZE);
		setVoxel(x, y, z);
		modeStage = 0;
		timer = 0;
		finishLoading();
	}

	// XXX: temporary variant
	// TODO: use Y-axis of joystick 2
	j2Button.tick();
	if (j2Button.isDouble()) {
		clearVoxel(x, y, z);
		z = shiftCoordinate(z, 1);
		setVoxel(x, y, z);
	} else if (j2Button.isSingle()) {
		clearVoxel(x, y, z);
		z = shiftCoordinate(z, -1);
		setVoxel(x, y, z);
	}

	// check timer
	timer++;
	if (timer < modeTimer) {
		// not time to change
		return;
	}

	// draw changes
	timer = 0;
	clearVoxel(x, y, z);
	x = shiftCoordinate(x, getJoystickMove(X_AXIS, J2_X_PIN));
	y = shiftCoordinate(y, getJoystickMove(Y_AXIS, J2_Y_PIN));
	setVoxel(x, y, z);
}

///////// COMMON METHODS ////////////

void setVoxel(byte x, byte y, byte z) {
	byte color;
	if (currentColor == COLORR) {
		color = random(0, 3);
	} else if (currentColor == COLORM) {
		color = currentMultiColor;
	} else {
		color = currentColor;
	}
	setVoxel(x, y, z, color);
}

void setVoxel(byte x, byte y, byte z, byte color) {
	if (color == COLOR1 || color == COLOR3) {
		cubes[COLOR1][y][z] |= (0x01 << x);
	}
	if (color == COLOR2 || color == COLOR3) {
		cubes[COLOR2][y][z] |= (0x01 << x);
	}
}

void clearVoxel(byte x, byte y, byte z) {
	cubes[COLOR1][y][z] &= (0x01 << x) ^ 0xffff;
	cubes[COLOR2][y][z] &= (0x01 << x) ^ 0xffff;
}

byte shiftCoordinate(byte coord, char delta) {
	char res = coord + delta;
	if (res < 0) {
		return 0;
	}
	if (res > CUBE_SIZE - 1) {
		return CUBE_SIZE - 1;
	}
	return res;
}

void changeCurrentArrayVoxel(bool switchOn) {
	// @formatter:off
	byte z = voxelsArray[voxelIndex] / (CUBE_SIZE * CUBE_SIZE);
	byte y = (voxelsArray[voxelIndex] % (CUBE_SIZE * CUBE_SIZE)) / CUBE_SIZE;
	byte x = (voxelsArray[voxelIndex] % (CUBE_SIZE * CUBE_SIZE)) % CUBE_SIZE;
	if (switchOn) {
		setVoxel(x, y, z);
	} else {
		clearVoxel(x, y, z);
	}
	// @formatter:on
}

void shift(byte direction) {
	// shift all voxels in specified direction
	switch (direction) {
		case X_POS:
			for (byte y = 0; y < CUBE_SIZE; y++) {
				for (byte z = 0; z < CUBE_SIZE; z++) {
					cubes[COLOR1][y][z] = cubes[COLOR1][y][z] << 1;
					cubes[COLOR2][y][z] = cubes[COLOR2][y][z] << 1;
				}
			}
			break;
		case X_NEG:
			for (byte y = 0; y < CUBE_SIZE; y++) {
				for (byte z = 0; z < CUBE_SIZE; z++) {
					cubes[COLOR1][y][z] = cubes[COLOR1][y][z] >> 1;
					cubes[COLOR2][y][z] = cubes[COLOR2][y][z] >> 1;
				}
			}
			break;
		case Y_POS:
			for (byte y = CUBE_SIZE - 1; y > 0; y--) {
				for (byte z = 0; z < CUBE_SIZE; z++) {
					cubes[COLOR1][y][z] = cubes[COLOR1][y - 1][z];
					cubes[COLOR2][y][z] = cubes[COLOR2][y - 1][z];
				}
			}
			for (byte z = 0; z < CUBE_SIZE; z++) {
				cubes[COLOR1][0][z] = 0;
				cubes[COLOR2][0][z] = 0;
			}
			break;
		case Y_NEG:
			for (byte y = 1; y < CUBE_SIZE; y++) {
				for (byte z = 0; z < CUBE_SIZE; z++) {
					cubes[COLOR1][y - 1][z] = cubes[COLOR1][y][z];
					cubes[COLOR2][y - 1][z] = cubes[COLOR2][y][z];
				}
			}
			for (byte z = 0; z < CUBE_SIZE; z++) {
				cubes[COLOR1][CUBE_SIZE - 1][z] = 0;
				cubes[COLOR2][CUBE_SIZE - 1][z] = 0;
			}
			break;
		case Z_POS:
			for (byte y = 0; y < CUBE_SIZE; y++) {
				for (byte z = CUBE_SIZE - 1; z > 0; z--) {
					cubes[COLOR1][y][z] = cubes[COLOR1][y][z - 1];
					cubes[COLOR2][y][z] = cubes[COLOR2][y][z - 1];
				}
			}
			for (byte y = 0; y < CUBE_SIZE; y++) {
				cubes[COLOR1][y][0] = 0;
				cubes[COLOR2][y][0] = 0;
			}
			break;
		case Z_NEG:
			for (byte y = 0; y < CUBE_SIZE; y++) {
				for (byte z = 1; z < CUBE_SIZE; z++) {
					cubes[COLOR1][y][z - 1] = cubes[COLOR1][y][z];
					cubes[COLOR2][y][z - 1] = cubes[COLOR2][y][z];
				}
			}
			for (byte y = 0; y < CUBE_SIZE; y++) {
				cubes[COLOR1][y][CUBE_SIZE - 1] = 0;
				cubes[COLOR2][y][CUBE_SIZE - 1] = 0;
			}
			break;
	}
}

int getJoystickMove(byte axis, byte axisPin) {
	byte invert = axis == X_AXIS ? J_INVERT_X : J_INVERT_Y;
	int value = analogRead(axisPin);
	if (value > J_HIGH) {
		return invert ? -1 : 1;
		return invert ? -1 : 1;
	} else if (value < J_LOW) {
		return invert ? 1 : -1;
	}
	return 0;
}

// clear cubes data
void clear() {
	for (byte color = 0; color < 2; color++) {
		for (byte lay = 0; lay < CUBE_SIZE; lay++) {
			for (byte row = 0; row < CUBE_SIZE; row++) {
				cubes[color][lay][row] = 0;
			}
		}
	}
}

void changeColor(byte color) {
	// TODO: check previous color
	// TODO: and move voxels from one cube to another of smth like that
}

void nextMultiColor() {
	currentMultiColor++;
	if (currentMultiColor > COLOR3) {
		currentMultiColor = COLOR1;
	}
}

void render() {
	short currentValue;
	short transRegValue = 0x00;
	short bits[CUBE_SIZE];
	byte regBitIndex = 0;
	short dataRegValues[DATA_REGISTERS_NUM];
	byte currentRegisterIndex;
	short currentRegisterValue;

	for (byte lay = 0; lay < CUBE_SIZE; lay++) {
		blankCube();
		digitalWrite(LATCH_PIN, LOW);
		// choose layer
		transRegValue = 0x01 << (INVERT_Y ? CUBE_SIZE - 1 - lay : lay);
		if (cubeIsBig) {
			// has 2 registers for layers
			SPI.transfer(transRegValue >> 8);
			SPI.transfer(transRegValue & 0x0f);
		} else {
			SPI.transfer(transRegValue);
		}

		// render layer
		currentRegisterIndex = DATA_REGISTERS_NUM - 1;
		currentRegisterValue = 0x00;
		for (byte cube = 0; cube < 2; cube++) {
			for (byte row = 0; row < CUBE_SIZE; row++) {
				currentValue = cubes[cube][lay][INVERT_Z ? CUBE_SIZE - 1 - row : row];

				// render columns in row
				// - fill operative bits array with value bits
				for (byte i = 0; i < CUBE_SIZE; i++) {
					bits[INVERT_X ? CUBE_SIZE - 1 - i : i] = (currentValue & 0x01) << BIT_DEPTH - 1;
					currentValue >>= 1;
				}
				// - fill value for register
				for (byte i = 0; i < CUBE_SIZE; i++) {
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
	blankCube();
}

// used for clean redraw
void blankCube() {
	digitalWrite(LATCH_PIN, LOW);
	if (cubeIsBig) {
		SPI.transfer(0x00);
	}
	for (byte i = 0; i < DATA_REGISTERS_NUM + 1; i++) {
		SPI.transfer(0x00);
	}
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
