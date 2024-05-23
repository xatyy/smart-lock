/************************************************************************
* Program:    SmartLock.ino
* Author:     Anthony Esber & Robert Negre
************************************************************************/

/* Wire.h Library used to write in registers and read sonar's analog sensor data */
#include <Wire.h>

/* EEPROM used for storing and reading onto the chip the User's PIN */
#include <EEPROM.h>

/* Constants setup for our required components within our project */

// Define the pins for the keypad rows and columns
#define PIN_R0 12
#define PIN_R1 11
#define PIN_R2 10
#define PIN_R3 9
#define PIN_C0 8
#define PIN_C1 7
#define PIN_C2 6
#define PIN_C3 5

// Define the pin for the relay
#define PIN_RELAY 4

// Keypad layout
const char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

char correctPIN[5] = "1234"; // Default PIN if not found on EEPROM
char enteredPIN[5] = ""; // Buffer to store entered PIN

// Timing variables
unsigned long previousMillis = 0; // will store last time key was checked
const long interval = 1; // interval at which to check the keys (milliseconds)
int currentRow = 0; // Current row being checked in the keypad
const int debounceDelay = 50; // debounce delay in milliseconds
bool keyPressed = false; // Tracks if a key is pressed
unsigned long lastDebounceTime = 0; // the last time the key was pressed

char keyBuffer[16]; // buffer to store keys pressed
int keyBufferIndex = 0; // Index for the key buffer

// Timer variables for auto logout
unsigned long countdownStartTime = 0;
bool countdownActive = false;
bool pinCorrectState = false; // Track "PIN correct" state

#define pinOut 13 // Define pinOut for pin 13

// LCD PIN and REGISTERS Setup
#define MCP23017_ADDR 0x20

#define IODIRA 0x00
#define IODIRB 0x01
#define GPIOA 0x12
#define GPIOB 0x13
#define OLATA 0x14
#define OLATB 0x15

#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET 0x20
#define LCD_SETDDRAMADDR 0x80

#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_5x8DOTS 0x00

#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSOROFF 0x00
#define LCD_BLINKOFF 0x00

#define LCD_WIDTH 16 

// SONAR Sensor Setup
const int trigPin = 2;
const int echoPin = 3;

float duration, distance;

bool passwordScreenShown = false; // Tracks if the password screen is shown
bool screenCleared = true; // Tracks if the screen is cleared

// Variables for scrolling text on LCD
unsigned long scrollStartTime = 0; // to manage scrolling timing
const long scrollDelay = 450; // delay between scroll steps
int scrollIndex = 0; // current scroll position
const char * scrollTextStr = "A - Operate relay, B - Change Password, C - Log out"; // Default Main Menu Text

#define EEPROM_ADDRESS 0 // Starting address in EEPROM to store the password
#define EEPROM_FLAG_ADDRESS 4 // Address to store the flag indicating a valid password

// Variables for tracking attempts
int pinAttempts = 0; // Track the number of incorrect PIN attempts
unsigned long lockoutStartTime = 0; // Start time of lockout
const int maxAttempts = 3; // Maximum number of attempts before lockout
long lockoutDuration = 30000; // 30 seconds lockout duration

// Set up the rows for reading
void setupForReadingRow(int r) {
  digitalWrite(PIN_R0, r != 0 ? HIGH : LOW);
  digitalWrite(PIN_R1, r != 1 ? HIGH : LOW);
  digitalWrite(PIN_R2, r != 2 ? HIGH : LOW);
  digitalWrite(PIN_R3, r != 3 ? HIGH : LOW);
}

// Read the columns of the keypad to detect key press
int readColumnOfKeyPress() {
  int c0 = digitalRead(PIN_C0);
  int c1 = digitalRead(PIN_C1);
  int c2 = digitalRead(PIN_C2);
  int c3 = digitalRead(PIN_C3);

  int c = -1;
  if (c0 == LOW) {
    c = 0;
  } else if (c1 == LOW) {
    c = 1;
  } else if (c2 == LOW) {
    c = 2;
  } else if (c3 == LOW) {
    c = 3;
  }
  return c;
}

// Get the key press from the keypad
char getKeyPress() {
  for (int row = 0; row < 4; row++) {
    setupForReadingRow(row);
    int col = readColumnOfKeyPress();
    if (col != -1) {
      while (readColumnOfKeyPress() != -1); // Wait for key release
      return keys[row][col];
    }
  }
  return '\0'; // No key pressed
}

// Encrypt the password using a simple Caesar cipher
void encryptPassword(char * password, char * encrypted) {
  for (int i = 0; i < 4; i++) {
    encrypted[i] = password[i] + 3; // Simple Caesar cipher with a shift of 3
  }
  encrypted[4] = '\0';
}

// Decrypt the password using a simple Caesar cipher
void decryptPassword(char * encrypted, char * password) {
  for (int i = 0; i < 4; i++) {
    password[i] = encrypted[i] - 3; // Simple Caesar cipher with a shift of 3
  }
  password[4] = '\0';
}

// Save the password to EEPROM
void savePassword(const char * password) {
  char encrypted[5];
  encryptPassword((char *) password, encrypted);
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_ADDRESS + i, encrypted[i]);
  }
  EEPROM.write(EEPROM_FLAG_ADDRESS, 1); // Set the flag indicating a valid password is stored
  Serial.print("Password saved: ");
  Serial.println(password);
}

// Read the password from EEPROM
void readPassword(char * password) {
  char encrypted[5];
  for (int i = 0; i < 4; i++) {
    encrypted[i] = EEPROM.read(EEPROM_ADDRESS + i);
  }
  encrypted[4] = '\0';
  decryptPassword(encrypted, password);
  Serial.print("Password read: ");
  Serial.println(password);
}

// Check if a password is set in EEPROM
bool isPasswordSet() {
  return EEPROM.read(EEPROM_FLAG_ADDRESS) == 1;
}

// Initialize EEPROM with default password if not set
void initializeEEPROM() {
  if (!isPasswordSet()) {
    savePassword("1234");
  }
}

// Initialize the pins for the components
void initializePins() {
  pinMode(PIN_R0, OUTPUT);
  pinMode(PIN_R1, OUTPUT);
  pinMode(PIN_R2, OUTPUT);
  pinMode(PIN_R3, OUTPUT);
  pinMode(PIN_C0, INPUT_PULLUP);
  pinMode(PIN_C1, INPUT_PULLUP);
  pinMode(PIN_C2, INPUT_PULLUP);
  pinMode(PIN_C3, INPUT_PULLUP);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, HIGH);
}

// Initialize the MCP23017 I/O expander
void initializeMCP23017() {
  writeRegister(MCP23017_ADDR, IODIRA, 0xff); // Set all of bank A to outputs
  writeRegister(MCP23017_ADDR, IODIRB, 0x01); // Set all of bank B to outputs
}

// Initialize the LCD
void initializeLCD() {
  delay(50); // Wait for LCD to power up

  // Follow the recommended initialization sequence
  lcdCommand(0x03); // Initialize LCD in 8-bit mode
  delayMicroseconds(4500);
  lcdCommand(0x03); // Initialize LCD in 8-bit mode
  delayMicroseconds(4500);
  lcdCommand(0x03); // Initialize LCD in 8-bit mode
  delayMicroseconds(150);
  lcdCommand(0x02); // Initialize LCD in 4-bit mode

  // Function set: 4-bit mode, 2 lines, 5x8 font
  lcdCommand(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS);

  // Display control: display on, cursor off, blink off
  lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF);

  // Clear display
  lcdCommand(LCD_CLEARDISPLAY);
  delayMicroseconds(2000); // This command takes a long time

  // Entry mode set: increment automatically, no display shift
  lcdCommand(LCD_ENTRYMODESET | 0x02);

  // Initial message
  lcdPrint("System up!");
  delay(2000);
  lcdCommand(LCD_CLEARDISPLAY);
  delayMicroseconds(2000);
}

// Display scrolling text on the LCD
void displayScrollText(const char * text, int start) {
  int len = strlen(text);
  for (int i = 0; i < LCD_WIDTH; i++) {
    if (start + i < len) {
      writeChar(text[start + i]);
    } else {
      writeChar(' ');
    }
  }
}

// Send command to the LCD
void lcdCommand(uint8_t command) {
  write4bits((command & 0xF0) >> 4, false);
  write4bits(command & 0x0F, false);
  delayMicroseconds(2000); // commands need > 37us to settle
}

// Print a string on the LCD
void lcdPrint(const char * str) {
  int count = 0;
  while (*str) {
    if (count == LCD_WIDTH) {
      // Scroll the remaining text if it doesn't fit in one line
      scrollText(str, 1, 300); // Adjust the delay as needed
      return;
    }
    writeChar(*str++);
    count++;
  }
}

// Write a single character to the LCD
void writeChar(uint8_t value) {
  write4bits((value & 0xF0) >> 4, true);
  write4bits(value & 0x0F, true);
}

// Write 4 bits to the LCD
void write4bits(uint8_t value, bool isData) {
  uint8_t out = 0;

  // Map the bits to the corresponding GPB pins
  if (value & 0x01) out |= (1 << 4); // D4 -> GPB4
  if (value & 0x02) out |= (1 << 3); // D5 -> GPB3
  if (value & 0x04) out |= (1 << 2); // D6 -> GPB2
  if (value & 0x08) out |= (1 << 1); // D7 -> GPB1

  if (isData) {
    out |= 0x80; // RS -> GPB7 (1 for data)
  } else {
    out &= ~0x80; // RS -> GPB7 (0 for command)
  }

  // RW is always 0 (write mode)
  out &= ~0x40; // RW -> GPB6 (0 for write)

  // Send data to LCD
  writeRegister(MCP23017_ADDR, GPIOB, out | 0x20); // EN high -> GPB5
  delayMicroseconds(1); // Ensure the EN pin is high for a short period
  writeRegister(MCP23017_ADDR, GPIOB, out & ~0x20); // EN low -> GPB5
  delayMicroseconds(100); // Commands need > 37us to settle
}

// Scroll text on the LCD
void scrollText(const char * text, int row, int delayTime) {
  int textLength = strlen(text);
  int displayWidth = LCD_WIDTH;

  if (textLength <= displayWidth) {
    lcdCommand(LCD_SETDDRAMADDR | (row == 0 ? 0x00 : 0x40));
    lcdPrint(text);
    return;
  }

  for (int i = 0; i <= textLength - displayWidth; i++) {
    lcdCommand(LCD_SETDDRAMADDR | (row == 0 ? 0x00 : 0x40));
    for (int j = 0; j < displayWidth; j++) {
      if (i + j < textLength) {
        writeChar(text[i + j]);
      } else {
        writeChar(' ');
      }
    }
    delay(delayTime);
  }

  // Scroll the remaining part
  for (int i = 0; i < displayWidth; i++) {
    lcdCommand(LCD_SETDDRAMADDR | (row == 0 ? 0x00 : 0x40));
    for (int j = 0; j < displayWidth; j++) {
      if (textLength - displayWidth + i + j < textLength) {
        writeChar(text[textLength - displayWidth + i + j]);
      } else {
        writeChar(' ');
      }
    }
    delay(delayTime);
  }
}

// Write a value to a register of the MCP23017
void writeRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

// Measure the distance using the SONAR sensor
void measureDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = (duration * 0.0343) / 2;
}

// Handle the correct PIN state
void handlePinCorrectState() {
  if (millis() - scrollStartTime >= scrollDelay) {
    scrollStartTime = millis();
    lcdCommand(LCD_SETDDRAMADDR | 0x00); // Move cursor to the first line
    displayScrollText(scrollTextStr, scrollIndex);
    scrollIndex++;
    if (scrollIndex >= strlen(scrollTextStr)) {
      scrollIndex = 0; // Reset scroll index
    }
  }

  if (distance > 180 && !countdownActive) {
    // Start countdown if person moves away
    countdownStartTime = millis();
    countdownActive = true;
  } else if (distance <= 180 && countdownActive) {
    // Cancel countdown if person comes back
    countdownActive = false;
    lcdCommand(LCD_SETDDRAMADDR | 0x40); // Move cursor to the second line
    lcdPrint("                "); // Clear the second line
  }

  if (countdownActive) {
    unsigned long elapsedTime = millis() - countdownStartTime;
    unsigned long remainingTime = 10000 - elapsedTime;

    // Display remaining time
    lcdCommand(LCD_SETDDRAMADDR | 0x40); // Move cursor to the second line
    lcdPrint("Logout in: ");
    lcdPrint(String(remainingTime / 1000).c_str());
    lcdPrint(" ");

    if (elapsedTime >= 10000) { // 10 seconds
      // Log out if countdown reaches 10 seconds
      pinCorrectState = false;
      lcdCommand(LCD_CLEARDISPLAY);
      delayMicroseconds(2000);
      lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYOFF);
      keyBufferIndex = 0;
      enteredPIN[0] = '\0'; // Reset the entered PIN
      passwordScreenShown = false;
      screenCleared = true;
      countdownActive = false;
      scrollIndex = 0; // Reset scroll index
    }
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    char key = getKeyPress();
    if (key != '\0') {
      if (!keyPressed || (millis() - lastDebounceTime > debounceDelay)) {
        lastDebounceTime = millis();
        keyPressed = true;
        Serial.println(String("You pressed [") + String(key) + String("]"));
        if (key == 'A') {
          // Handle 'A' press
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          lcdPrint("                "); // Clear the second line
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          lcdPrint("Relay operating");
          digitalWrite(PIN_RELAY, LOW);
          delay(1000); // Short delay to simulate a pulse
          digitalWrite(PIN_RELAY, HIGH);
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          lcdPrint("                "); // Clear the second line
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
        } else if (key == 'B') {
          // Handle 'B' press to save new password
          changePassword();
        } else if (key == 'C') {
          // Handle 'C' press to log out
          pinCorrectState = false;
          keyBufferIndex = 0;
          enteredPIN[0] = '\0'; // Reset the entered PIN
          lcdCommand(LCD_CLEARDISPLAY);
          lcdPrint("Enter Password:    ");
        }
      }
    } else {
      keyPressed = false;
    }

    currentRow++;
    if (currentRow > 3) {
      currentRow = 0;
    }
  }
}

// Change the password procedure
void changePassword() {
  char newPassword[5] = "";
  char confirmPassword[5] = "";

  // Enter new password
  lcdCommand(LCD_CLEARDISPLAY);
  lcdPrint("New Password:    ");
  delay(1000); // Short delay for readability
  keyBufferIndex = 0;
  while (keyBufferIndex < 4) {
    char key = getKeyPress();
    if (key != '\0') {
      if (key == 'D') {
        // Handle deletion
        if (keyBufferIndex > 0) {
          keyBufferIndex--;
          enteredPIN[keyBufferIndex] = '\0';
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          lcdPrint("                "); // Clear the second line
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          for (int i = 0; i < keyBufferIndex; i++) {
            lcdPrint("* ");
          }
        }
      } else if (key >= '0' && key <= '9') {
        newPassword[keyBufferIndex++] = key;
        lcdCommand(LCD_SETDDRAMADDR | 0x40);
        lcdPrint("                "); // Clear the second line
        lcdCommand(LCD_SETDDRAMADDR | 0x40);
        for (int i = 0; i < keyBufferIndex; i++) {
          lcdPrint("* ");
        }
      }
      delay(200); // Debounce delay
    }
  }
  newPassword[keyBufferIndex] = '\0'; // Null-terminate the buffer

  // Confirm new password
  lcdCommand(LCD_CLEARDISPLAY);
  lcdPrint("Confirm Password");
  delay(1000); // Short delay for readability
  keyBufferIndex = 0;
  while (keyBufferIndex < 4) {
    char key = getKeyPress();
    if (key != '\0') {
      if (key == 'D') {
        // Handle deletion
        if (keyBufferIndex > 0) {
          keyBufferIndex--;
          enteredPIN[keyBufferIndex] = '\0';
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          lcdPrint("                "); // Clear the second line
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          for (int i = 0; i < keyBufferIndex; i++) {
            lcdPrint("* ");
          }
        }
      } else if (key >= '0' && key <= '9') {
        confirmPassword[keyBufferIndex++] = key;
        lcdCommand(LCD_SETDDRAMADDR | 0x40);
        lcdPrint("                "); // Clear the second line
        lcdCommand(LCD_SETDDRAMADDR | 0x40);
        for (int i = 0; i < keyBufferIndex; i++) {
          lcdPrint("* ");
        }
      }
      delay(200); // Debounce delay
    }
  }
  confirmPassword[keyBufferIndex] = '\0'; // Null-terminate the buffer

  // Check if passwords match
  if (strcmp(newPassword, confirmPassword) == 0) {
    savePassword(newPassword); // Save the new password to EEPROM
    readPassword(correctPIN); // Read the updated password from EEPROM
    lcdCommand(LCD_CLEARDISPLAY);
    lcdPrint("Password Saved");
  } else {
    lcdCommand(LCD_CLEARDISPLAY);
    lcdPrint("Passwords do not match");
  }
  delay(2000); // Display message for 2 seconds
  lcdCommand(LCD_CLEARDISPLAY);
  lcdPrint("Enter Password:    ");
}

// Handle the incorrect PIN state
void handlePinIncorrectState() {
  if (distance < 150 && pinAttempts < maxAttempts) {
    writeRegister(MCP23017_ADDR, IODIRA, 0x7f);
    if (!passwordScreenShown) {
      // Display password screen
      lcdCommand(LCD_CLEARDISPLAY);
      delayMicroseconds(2000);
      lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
      lcdPrint("Enter Password:    ");

      passwordScreenShown = true;
      screenCleared = false;
    } else if (countdownActive) {
      // Cancel countdown and return to password prompt
      lcdCommand(LCD_CLEARDISPLAY);
      delayMicroseconds(2000);
      lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
      lcdPrint("Enter Password:    ");
      lcdCommand(LCD_SETDDRAMADDR | 0x40);
      for (int i = 0; i < keyBufferIndex; i++) {
        lcdPrint("* ");
      }
      countdownActive = false;
    }
  } else if (distance > 180 && pinAttempts < maxAttempts) {
    if (!countdownActive && !screenCleared) {
      delay(500);
      // Display "Person not detected" message and start countdown
      lcdCommand(LCD_CLEARDISPLAY);
      delayMicroseconds(2000);
      lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
      scrollText("Out of reach", 0, 300); // Adjust the delay as needed
      countdownStartTime = millis();
      countdownActive = true;
    } else if (countdownActive) {
      // Calculate remaining time
      unsigned long elapsedTime = millis() - countdownStartTime;
      unsigned long remainingTime = 5000 - elapsedTime;

      // Display remaining time
      lcdCommand(LCD_SETDDRAMADDR | 0x40); // Move cursor to the second line
      lcdPrint("(");
      lcdPrint(String(remainingTime / 1000).c_str());
      lcdPrint(" s)");

      if (elapsedTime >= 5000) {
        // Timeout reached, clear the screen
        lcdCommand(LCD_CLEARDISPLAY);
        delayMicroseconds(2000);
        lcdCommand(LCD_DISPLAYCONTROL | LCD_DISPLAYOFF);
        writeRegister(MCP23017_ADDR, IODIRA, 0xff);
        passwordScreenShown = false;
        screenCleared = true;
        countdownActive = false;
      }
    }
  }

  unsigned long currentMillis = millis();
  if (passwordScreenShown && currentMillis - previousMillis >= interval && pinAttempts < maxAttempts) {
    previousMillis = currentMillis;

    char key = getKeyPress();
    if (key != '\0') {
      if (!keyPressed || (millis() - lastDebounceTime > debounceDelay)) {
        lastDebounceTime = millis();
        keyPressed = true;
        Serial.println(String("You pressed [") + String(key) + String("]"));
        if (key == 'D') {
          // Handle deletion
          if (keyBufferIndex > 0) {
            keyBufferIndex--;
            enteredPIN[keyBufferIndex] = '\0';
            lcdCommand(LCD_SETDDRAMADDR | 0x40);
            lcdPrint("                "); // Clear the second line
            lcdCommand(LCD_SETDDRAMADDR | 0x40);
            for (int i = 0; i < keyBufferIndex; i++) {
              lcdPrint("* ");
            }
          }
        } else if (keyBufferIndex < 4 && (key >= '0' && key <= '9')) {
          enteredPIN[keyBufferIndex++] = key;
          enteredPIN[keyBufferIndex] = '\0'; // Null-terminate the buffer
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          lcdPrint("                "); // Clear the second line
          lcdCommand(LCD_SETDDRAMADDR | 0x40);
          for (int i = 0; i < keyBufferIndex; i++) {
            lcdPrint("* ");
          }
        }
        if (keyBufferIndex == 4) {
          Serial.print("Entered PIN: ");
          Serial.println(enteredPIN);
          Serial.print("Correct PIN: ");
          Serial.println(correctPIN);
          if (strcmp(enteredPIN, correctPIN) == 0) {
            pinAttempts = 0;
            lockoutDuration = 30000;
            writeRegister(MCP23017_ADDR, IODIRA, 0xff);
            writeRegister(MCP23017_ADDR, IODIRB, 0x0);
            lcdCommand(LCD_CLEARDISPLAY);
            lcdPrint("Welcome!");
            delay(2000);
            writeRegister(MCP23017_ADDR, IODIRB, 0x1);
            pinCorrectState = true;
            scrollIndex = 0; // Reset scroll index
            pinAttempts = 0; // Reset attempts counter
          } else {
            pinAttempts++;
            lcdCommand(LCD_SETDDRAMADDR | 0x00); // Move cursor to the first line
            lcdPrint("                ");
            lcdCommand(LCD_SETDDRAMADDR | 0x00);
            lcdPrint("PIN incorrect");
            lcdCommand(LCD_SETDDRAMADDR | 0x40);
            lcdPrint("                "); // Clear the second line
            lcdCommand(LCD_SETDDRAMADDR | 0x40);
            writeRegister(MCP23017_ADDR, IODIRA, 0xbf);
            delay(2000); // Display the result for 2 seconds

            if (pinAttempts >= maxAttempts) {
              lockoutStartTime = millis();
              lcdCommand(LCD_SETDDRAMADDR | 0x00); // Move cursor to the first line
              lcdPrint("                ");
              lcdCommand(LCD_SETDDRAMADDR | 0x00);
              lcdPrint("Locked out");
            } else {
              lcdCommand(LCD_SETDDRAMADDR | 0x00); // Move cursor to the first line
              lcdPrint("                ");
              lcdCommand(LCD_SETDDRAMADDR | 0x00);
              lcdPrint("Enter Password:");
            }

            keyBufferIndex = 0;
            enteredPIN[0] = '\0'; // Reset the entered PIN
          }
        }
      }
    } else {
      keyPressed = false;
    }

    currentRow++;
    if (currentRow > 3) {
      currentRow = 0;
    }
  } else if (pinAttempts >= maxAttempts) {
    // Check if lockout period has expired
    unsigned long elapsedLockoutTime = millis() - lockoutStartTime;
    if (elapsedLockoutTime >= lockoutDuration) {
      pinAttempts = 2;
      lockoutDuration *= 2;
      lcdCommand(LCD_SETDDRAMADDR | 0x40);
      lcdPrint("                "); // Clear the second line
      lcdCommand(LCD_SETDDRAMADDR | 0x40);
      lcdCommand(LCD_SETDDRAMADDR | 0x00); // Move cursor to the first line
      lcdPrint("Enter Password:");
    } else {
      // Display remaining lockout time
      lcdCommand(LCD_SETDDRAMADDR | 0x40); // Move cursor to the second line
      lcdPrint("Retry in: ");
      lcdPrint(String((lockoutDuration - elapsedLockoutTime) / 1000).c_str());
      lcdPrint(" s  ");
    }
  }
}

// Setup function, runs once when the program starts
void setup() {
  Serial.begin(9600);
  Wire.begin();

  initializePins(); // Initialize all pins
  initializeMCP23017(); // Initialize the MCP23017 I/O expander
  initializeLCD(); // Initialize the LCD

  // Initialize and read the saved password from EEPROM
  initializeEEPROM(); // Initialize EEPROM with default password if not set
  readPassword(correctPIN); // Read the saved password
}

// Main loop, runs continuously
void loop() {
  if (pinAttempts < maxAttempts) {
    measureDistance(); // Measure distance using SONAR sensor
  }

  if (pinCorrectState) {
    handlePinCorrectState(); // Handle state when PIN is correct
  } else {
    handlePinIncorrectState(); // Handle state when PIN is incorrect
  }

  delay(25); // Adjust the delay as needed to avoid flickering
}
