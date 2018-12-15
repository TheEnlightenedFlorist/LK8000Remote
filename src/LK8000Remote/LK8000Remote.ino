#include <bluefruit.h>

//joystick
#define JOYSTICK_DEADZONE  (.8)
#define JOY_X_PIN (A0)
#define JOY_Y_PIN (A1)
#define JOY_MAX_VALUE (1023)

//button pins
#define CUSTOM_KEY_LEFT_PIN (A3)
#define CUSTOM_KEY_RIGHT_PIN (A3)
#define BB_LEFT_PIN (A3)
#define NEXT_PAGE_PIN (A2)
#define BB_RIGHT_PIN (A3)
#define ENTER_PIN (A4)

//HID codes for each button
#define CUSTOM_KEY_LEFT (HID_KEY_9) //'9'
#define CUSTOM_KEY_RIGHT (HID_KEY_1) //'l'
#define UP (HID_KEY_H) //'h'
#define DOWN (HID_KEY_G) //'g'
#define LEFT (HID_KEY_5) //'5'
#define RIGHT (HID_KEY_SPACE) // spacebar
#define BB_LEFT (HID_KEY_2) //'2'
#define NEXT_PAGE (HID_KEY_A) //'a'
#define BB_RIGHT (HID_KEY_Z) //'z'
#define ENTER (KEYBOARD_MODIFIER_LEFTSHIFT) //modifier code

#define DELAY (100) //delay between joystick reads in the main loop.
#define DEBOUNCE_TIME (125) //Debouncing Time in Milliseconds

#define TX_POWER (4) //Power level of BT transmitter. Accepted values are: -20, -16, -12, -8, -4, 0, 4 in dBm.

//represents the directions of the joysticks axes
enum { LEFT_UP=-1, CENTER=0, RIGHT_DOWN=1 };

volatile unsigned long last_micros;

//Bluetooth device information service.
BLEDis bledis;

//The HID keyboard
BLEHidAdafruit keyboard;

//The previous position of the joystick axes. Used to determine when they've moved from center.
int x_prev_position;
int y_prev_position;

void setup() 
{
  //joystick starts in the center
  x_prev_position = CENTER;
  y_prev_position = CENTER;

  //Read it in 10-bit resolution 0-1023
  analogReadResolution(10);

  //pullup resistors on button input pins
  pinMode(CUSTOM_KEY_LEFT_PIN, INPUT_PULLUP);
  pinMode(CUSTOM_KEY_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BB_LEFT_PIN, INPUT_PULLUP);
  pinMode(NEXT_PAGE_PIN, INPUT_PULLUP);
  pinMode(BB_RIGHT_PIN, INPUT_PULLUP);
  pinMode(ENTER_PIN, INPUT_PULLUP);

  //Use interrupt callbacks on the falling edge of button pins
  attachInterrupt(digitalPinToInterrupt(CUSTOM_KEY_LEFT_PIN), customKeyLeftCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(CUSTOM_KEY_RIGHT_PIN), customKeyRightCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(BB_LEFT_PIN), bbLeftCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(NEXT_PAGE_PIN), nextPageCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(BB_RIGHT_PIN), bbRightCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(ENTER_PIN), enterCallback, ISR_DEFERRED | FALLING);
  
  Serial.begin(115200);
  
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("LK8000 Remote");

  // Configure and Start Device Information Service
  bledis.setManufacturer("Ouroboros Soaring");
  bledis.setModel("Bluefruit Feather 52");
  bledis.begin();

  /* Start BLE HID
   * Note: Apple requires BLE device must have min connection interval >= 20m
   * ( The smaller the connection interval the faster we could send data).
   * However for HID and MIDI device, Apple could accept min connection interval 
   * up to 11.25 ms. Therefore BLEHidAdafruit::begin() will try to set the min and max
   * connection interval to 11.25  ms and 15 ms respectively for best performance.
   */
  keyboard.begin();

  // Set callback for set LED from central
  keyboard.setKeyboardLedCallback(set_keyboard_led);

  /* Set connection interval (min, max) to your perferred value.
   * Note: It is already set by BLEHidAdafruit::begin() to 11.25ms - 15ms
   * min = 9*1.25=11.25 ms, max = 12*1.25= 15 ms 
   */
  /* Bluefruit.setConnInterval(9, 12); */

  // Set up and start advertising
  startAdv();
}

void startAdv(void)
{  
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
  
  // Include BLE HID service
  Bluefruit.Advertising.addService(keyboard);

  // There is enough room for the dev name in the advertising packet
  Bluefruit.Advertising.addName();
  
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

void loop() 
{
  readJoystick();
  delay(DELAY);
}

/**
 * Read the joystick values and send any appropriate keys.
 */
void readJoystick()
{
  int current_x_position = joyToDigital(analogRead(JOY_X_PIN));
  int current_y_position = joyToDigital(analogRead(JOY_Y_PIN));

  if(x_prev_position == CENTER && current_x_position != CENTER)
  {
    if(current_x_position == RIGHT_DOWN)
      keyboard.keyboardReport( {}, RIGHT);
    else
      keyboard.keyboardReport( {}, LEFT);

    keyboard.keyRelease();
  }

  if(y_prev_position == CENTER && current_y_position != CENTER)
  {
    if(current_y_position == RIGHT_DOWN)
      keyboard.keyboardReport( {}, DOWN);
    else
      keyboard.keyboardReport( {}, UP);

    keyboard.keyRelease();
  }

  x_prev_position = current_x_position;
  y_prev_position = current_y_position;
}

/**
 * Takes the raw output from the joystick axis pin and translates it to either
 * a -1, 0, or 1.
 */
int joyToDigital(int axisValue)
{
  int posThreshold = (JOY_MAX_VALUE / 2) + (JOY_MAX_VALUE * (JOYSTICK_DEADZONE / 2));
  int negThreshold = (JOY_MAX_VALUE / 2) - (JOY_MAX_VALUE * (JOYSTICK_DEADZONE / 2));

  if (axisValue < negThreshold)
    return LEFT_UP;
  if (axisValue > posThreshold)
    return RIGHT_DOWN;

  return CENTER;
}

/**
 * Callback invoked when received Set LED from central.
 * Must be set previously with setKeyboardLedCallback()
 *
 * The LED bit map is as follows: (also defined by KEYBOARD_LED_* )
 *    Kana (4) | Compose (3) | ScrollLock (2) | CapsLock (1) | Numlock (0)
 */
void set_keyboard_led(uint8_t led_bitmap)
{
  // light up Red Led if any bits is set
  if ( led_bitmap )
  {
    ledOn( LED_RED );
  }
  else
  {
    ledOff( LED_RED );
  }
}

//Callbacks for the various buttons
void customKeyLeftCallback(void)
{
  if(debounced()) 
  {
    Serial.print("Switch: ");
    Serial.println(digitalRead(CUSTOM_KEY_LEFT_PIN));
    
    keyboard.keyboardReport({}, CUSTOM_KEY_LEFT);
    keyboard.keyRelease();
    
    last_micros = micros();
  }
}

void customKeyRightCallback(void)
{
  
}

void bbLeftCallback(void)
{
  
}

void nextPageCallback(void)
{
  
}

void bbRightCallback(void)
{
  
}

void enterCallback(void)
{
  if(debounced()) 
  {
    Serial.print("Switch: ");
    Serial.println(digitalRead(ENTER_PIN));

//    keyboard.keyPress(0x2007);
//    keyboard.keyRelease();

    sendModOnly(ENTER);
    
    last_micros = micros();
  }
}

/**
 * Sends the modifier with a blank key code.
 */
void sendModOnly(uint8_t modifier)
{
  uint8_t code[6] = {HID_KEY_NONE};
  keyboard.keyboardReport(modifier, code);
  keyboard.keyRelease();
}

/**
 * If enough time has passed to be considered debounced.
 */
bool debounced(void)
{
  return (long)(micros() - last_micros) >= DEBOUNCE_TIME * 1000;
}
