#ifdef OLED_DISPLAY
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//int analogInput = A6;

/****************/
/* BATERRY ICON */
/****************/
#define LOGO_BAT_HEIGHT   9
#define LOGO_BAT_WIDTH    6

const unsigned char PROGMEM logo_bat[] =
{ 0b01111000,
  0b01111000,
  0b10000100,
  0b10000100,
  0b10000100,
  0b10000100,
  0b10000100,
  0b10000100,
  0b11111100 };

void dispaly_init() {
  //  pinMode(analogInput, INPUT);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    for (;;); // Don't proceed, loop forever
  }

  // Clear the buffer
  display.clearDisplay();

  display.display();

  display.drawRect(0, 0,  128 , 16, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(18, 4);
  display.println("- HUBSAN H107L -");

  display.drawBitmap(0, 23, logo_bat, LOGO_BAT_WIDTH, LOGO_BAT_HEIGHT, SSD1306_WHITE); // display.drawBitmap(x position, y position, bitmap data, bitmap width, bitmap height, color)

  display.setCursor(12, 25);
  display.println("0.0 V");
  
  display.display();
}

// Prints baterry voltage to the OLED display. The voltage is
// in decimal number in the range from 0 (0V) to 43 (4.3V).
void printVolts(uint8_t v_lipo)
{
  uint8_t result;
  uint8_t remainder;

  divmod10(v_lipo, result, remainder);
  
//  display.setTextSize(1);             // Normal 1:1 pixel scale
//  display.setTextColor(SSD1306_WHITE);        // Draw white text with transparent background
//  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);        // Draw white text with black background
  display.setCursor(12, 25);             // Start at the second row

  display.write('0' + result);
  //display.write('.');
  
  display.setCursor(24, 25);             // Start at the second row
  display.write('0' + remainder);
  //display.write(' ');
  //display.write('V');

  display.display();
}

// Fast divide and modulo by 10 in one function
// Code based upon book hackers delight
void divmod10(uint8_t in, uint8_t &div, uint8_t &mod)
{
  // q = in * 0.8;
  uint8_t q = (in >> 1) + (in >> 2);
  q = q + (q >> 4);
  //q = q + (q >> 8); // comment for 8 bit
  //q = q + (q >> 16); // comment for 16 bit

  // q = q / 8;  ==> q =  in *0.1;
  q = q >> 3;
  
  // determine error
  uint8_t  r = in - ((q << 3) + (q << 1));   // r = in - q*10; 
  div = q + (r > 9);
  if (r > 9) mod = r - 10;
  else mod = r;
}
#endif
