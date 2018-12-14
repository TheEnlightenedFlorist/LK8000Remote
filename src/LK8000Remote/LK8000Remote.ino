#include <bluefruit.h>

//joystick
#define JOYSTICK_DEADZONE  (200)
#define JOY_X_PIN (A0)
#define JOY_Y_PIN (A1)
#define JOY_SWITCH_PIN (A2)
#define JOY_MAX_VALUE (950)

//button pins
#define CUSTOM_KEY_LEFT_PIN (A3)
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

#define DELAY (100)

enum { LEFT_UP=-1, CENTER=0, RIGHT_DOWN=1 };

static unsigned long last_interrupt_time = 0;

long debouncing_time = 125; //Debouncing Time in Milliseconds
volatile unsigned long last_micros;

BLEDis bledis;
BLEHidAdafruit keyboard;

int x_prev_position;
int y_prev_position;

void setup() 
{
  x_prev_position = CENTER;
  y_prev_position = CENTER;
  
  pinMode(JOY_SWITCH_PIN, INPUT_PULLUP);
  pinMode(CUSTOM_KEY_LEFT_PIN, INPUT_PULLUP);
  pinMode(ENTER_PIN, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(JOY_SWITCH_PIN), joyButtonCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(CUSTOM_KEY_LEFT_PIN), customKeyLeftCallback, ISR_DEFERRED | FALLING);
  attachInterrupt(digitalPinToInterrupt(ENTER_PIN), enterCallback, ISR_DEFERRED | FALLING);
  
  Serial.begin(115200);
  
  Bluefruit.begin();
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
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

void readJoystick()
{
  int ox = analogRead(JOY_X_PIN);
  int oy = analogRead(JOY_Y_PIN);
                     // FYI: output = map(intput,from low,from high,to low,to high)
  int mx = map(ox,0,JOY_MAX_VALUE,-512,512);
  int my = map(oy,0,JOY_MAX_VALUE,-512,512);

  int dx = abs(mx) < JOYSTICK_DEADZONE ? 0 : mx;
  int dy = abs(my) < JOYSTICK_DEADZONE ? 0 : my;

  int fx = map(dx, -500, 500, -1, 1);
  int fy = map(dy, -500, 500, -1, 1);

  if(x_prev_position == CENTER && fx != CENTER)
  {
    Serial.println("Sending Joy dir.");
    
    if(fx == RIGHT_DOWN)
      keyboard.keyboardReport( {}, RIGHT);
    else
      keyboard.keyboardReport( {}, LEFT);

    keyboard.keyRelease();
  }

  if(y_prev_position == CENTER && fy != CENTER)
  {
    Serial.println("Sending Joy dir.");
    
    if(fy == RIGHT_DOWN)
      keyboard.keyboardReport( {}, DOWN);
    else
      keyboard.keyboardReport( {}, UP);

    keyboard.keyRelease();
  }

  x_prev_position = fx;
  y_prev_position = fy;
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

void joyButtonCallback(void)
{
  if((long)(micros() - last_micros) >= debouncing_time * 1000) 
  {
    if(!digitalRead(JOY_SWITCH_PIN))
    {
      Serial.println("Joy Switch: 0");
      
      keyboard.keyboardReport({}, NEXT_PAGE);
      keyboard.keyRelease();
      
      last_micros = micros();
    }
  }
}

void customKeyLeftCallback(void)
{
  if((long)(micros() - last_micros) >= debouncing_time * 1000) 
  {
    Serial.print("Switch: ");
    Serial.println(digitalRead(CUSTOM_KEY_LEFT_PIN));
    
    //keyboard.keyboardReport({}, CUSTOM_KEY_LEFT);
    keyboard.keyPress(0xD);
    keyboard.keyRelease();
    
    last_micros = micros();
  }
}

void enterCallback(void)
{
  if((long)(micros() - last_micros) >= debouncing_time * 1000) 
  {
    Serial.print("Switch: ");
    Serial.println(digitalRead(ENTER_PIN));

//    keyboard.keyPress(0x11);
//    keyboard.keyRelease();

    sendModOnly(ENTER);
    
    last_micros = micros();
  }
}

void sendModOnly(uint8_t modifier)
{
  uint8_t code[6] = {HID_KEY_NONE};
  keyboard.keyboardReport(modifier, code);
  keyboard.keyRelease();
}
