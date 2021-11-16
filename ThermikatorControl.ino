/***************************************************************************
  This is a library for the BMP280 humidity, temperature & pressure sensor

  Designed specifically to work with the Adafruit BMEP280 Breakout 
  ----> http://www.adafruit.com/products/2651

  These sensors use I2C or SPI to communicate, 2 or 4 pins are required 
  to interface.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit andopen-source hardware by purchasing products
  from Adafruit!

  Written by Limor Fried & Kevin Townsend for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
 ***************************************************************************/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DallasTemperature.h>
#include <Servo.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"


#define DEBUG false
#define DEBUGINTERVALL true

// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C

// Poti is connected to A0 Pin 18
#define POTI 18

// Data wire is plugged into pin 8 on the Arduino
#define ONE_WIRE_BUS 8

// reference pressure for calculation of absolute altitute
#define StandardPressure 1013.25

// Delay intervall value empirically determined
// This value must match the delay caused by measuring the temperature via one-Wire
#define MeasuringDelayIntervall 638

// The analoge input for poti fully depressend (MaxPoti) and idle (MinPoti)
// Those values are specific for the used poti and mechanic 
// lower and higher values might be possible, but this is the defined limit 
#define MaxPoti 980
#define MinPoti 370

// Absolute min. and max. values for PWM modulation that Opto Spin 180 will accept
#define MinPowerPWM 50
#define MaxPowerPWM 140

#define AutothrustPin 10

#define MaxMotorTemp 80

#define MaxClimbRate 0.6
#define ClimbHysterese 0.2

int PotiValue = 0;
int PotiValue1 = 0;
int PotiValue2 = 0;
int PotiValue3 = 0;
int PotiValue4 = 0;
int PotiSetPower = 0;
int AllowedPower = 0;
int LastAllowedPower = 0;
int PowerPWM = 0;   
int counter = 0;
int LastMillis = 0;
int CurrentMillis = 0;
int intervall = 0;
int Autothrust = 0;
int MaxAllowedPower = 0;
int ReducePower = 0;
int OverTemp = 0;
int RemainingAkku = 100;
int LastRemainingAkku = 100;

int Temperature = 0;
int LastTemperature = 0;

float Alt1 = 0;
float Alt2 = 0;
float Alt3 = 0;
float Alt4 = 0;
float AltAverage = 0;
float LastAltAverage = 0;
float Vario1 = 0;
float Vario2 = 0;
float Vario3 = 0;
float Vario4 = 0;
float VarioAverage = 0;

Adafruit_BMP280 bmp; // I2C

char Line1[12];
char Line2[12];
char Line3[12];
char Line4[12];


// Setup a oneWire instance to communicate with any OneWire devices 
// (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
 
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


Servo ServoPower;  // create servo object to control a servo

static char outstr[15];

SSD1306AsciiWire oled;
  
void setup() {

  #if DEBUG == true
    Serial.begin(9600);
    Serial.println("Thermikator Control");
  #endif
  
  Wire.begin();
  Wire.setClock(100000L);

  oled.begin(&Adafruit128x64, I2C_ADDRESS);

  oled.setFont(fixed_bold10x15); // 12 characters per line !
  oled.clear();
  oled.println("Thermikator");
  oled.println("  Control");

  // Start up the library
  sensors.begin();
 
  bmp.begin(BMP280_ADDRESS_ALT);

  #if DEBUG == true
    if (!bmp.begin(BMP280_ADDRESS_ALT)) {  
    Serial.println("no valid BME280");
  }
  #endif

  sensors.requestTemperatures(); // Send the command to get temperatures
  Temperature = sensors.getTempCByIndex(0);      

  Alt1 = bmp.readAltitude(StandardPressure);
  Alt2 = Alt1;
  Alt3 = Alt1;
  Alt4 = Alt1;
  LastAltAverage = Alt1;
  LastMillis = millis(); 
  
  ServoPower.attach(9);  // attaches the servo on pin 9 to the servo object
  ServoPower.write(MinPowerPWM);

  pinMode(AutothrustPin, INPUT);

}
  
void loop() {
    
    #if DEBUG == true
      Serial.print(F("Temperature = "));
      Serial.print(bmp.readTemperature());
      Serial.println(" *C");
    
      Serial.print(F("Pressure = "));
      Serial.print(bmp.readPressure());
      Serial.println(" hPa");
    
      Serial.print(F("Altitude = "));
      Serial.print(bmp.readAltitude(StandardPressure));
      Serial.println(" m MSL");
      
      Serial.print(F("Engine temp = "));
      Serial.print(Temperature);
      Serial.println(" *C");

    #endif

    // clear the display only once in the loop, because this takes 60 ms and causes flicker
    if (counter == 0 )
      oled.clear();
      
    LastTemperature   = Temperature;
    LastAllowedPower  = AllowedPower;
    LastRemainingAkku = RemainingAkku;
    
    oled.setCursor(0,0);
    oled.println(Line1);
    oled.println(Line2);
    oled.println(Line3);
    oled.println(Line4);
        
    Alt4 = Alt3;
    Alt3 = Alt2;
    Alt2 = Alt1;
        
    Alt1 = bmp.readAltitude(StandardPressure);
    LastAltAverage = AltAverage;
    AltAverage = (Alt1 + Alt2 + Alt3 +Alt4) / 4;
    
    // read the value from the sensor
    PotiValue4  = PotiValue3;
    PotiValue3  = PotiValue2;
    PotiValue2  = PotiValue1;
    PotiValue1  = analogRead(POTI);  

    PotiValue = (PotiValue1 + PotiValue2 + PotiValue3 + PotiValue4) / 4;

    // read the setting of the Autothrust switch
      
    Autothrust = digitalRead(AutothrustPin); 

    #if DEBUG == true
      Serial.print(F("Poti value = "));
      Serial.print(PotiValue);
      Serial.println();
    #endif

    // full power  -> poti fully depressed -> value 1050 or greater
    // power off   -> poti not activly depresses -> value 350 or less

    // we just want do deal with values between defined MinPoti and MaxPoti   
    
    if (PotiValue > MaxPoti) 
      PotiValue = MaxPoti;
    if (PotiValue < MinPoti) 
      PotiValue = MinPoti;
        
    // map the min/max of the poti to 0-100%
    
    PotiSetPower = map(PotiValue, MinPoti, MaxPoti, 0, 100);
    
    CurrentMillis = millis();
    intervall = CurrentMillis - LastMillis;
    LastMillis = CurrentMillis;

    // overwrite the oldest clim/sink rate value by shifting them
    
    Vario4 = Vario3;
    Vario3 = Vario2;
    Vario2 = Vario1;    
 
    // calculate the current climb/sink rate from altitude gain in current intervall
    
    Vario1 = (AltAverage - LastAltAverage) * (1000/intervall);
    
    // calculate average climb/sink rate

    VarioAverage = (Vario1 + Vario2 + Vario3 + Vario4) / 4;
    if (VarioAverage > 9.9)
      VarioAverage = 9.9;
    if (VarioAverage < 0)
      strcpy(Line1, "Vario -");
    else
      strcpy(Line1, "Vario  ");
      
    dtostrf(abs(VarioAverage), 2, 1, &Line1[strlen(Line1)]);    

 
    // before AT or overtemp calculations, allowed is what is set
    AllowedPower = PotiSetPower;
     
    if (Autothrust) 
    {
      if (VarioAverage > MaxClimbRate + ClimbHysterese)
      {
        if (ReducePower < 99) {
          ReducePower = ReducePower + 2;
          }
        }
      
      if (VarioAverage < MaxClimbRate - ClimbHysterese)
      {
       if (ReducePower > 1) {
          ReducePower = ReducePower - 2;
          }
      }
      
      AllowedPower = PotiSetPower - ReducePower;
      if (AllowedPower < 0 ) {
        AllowedPower = 0;
      }
      
    }

    // if MaxMotorTemp is exceeded, power setting is reduced
    if (Temperature > MaxMotorTemp)
    { 
      // every degree over the limit reduces power by 10% absolute power

      OverTemp = Temperature - MaxMotorTemp;
      if (OverTemp < 0)
      { 
        // surpress negative OverTemp
        OverTemp = 0;
      }
       
      MaxAllowedPower = 100 - (OverTemp * 10);
      if (MaxAllowedPower < 20)
      {
        // 80% reduction is the maximum - leave 20% for motor ventilation
        MaxAllowedPower = 20;  
      }
    
      AllowedPower = min(AllowedPower,MaxAllowedPower);
           
    }
    
    PowerPWM = map(AllowedPower, 0, 100, MinPowerPWM, MaxPowerPWM);  // scale  0 - 100% power setting to use it with PWM Signal of Opto Spin      
    ServoPower.write(PowerPWM);                                      // modulate PWM according to the scaled value
    

#if DEBUGINTERVALL == true
    Serial.print(F("Intervall = "));
    Serial.print(intervall);
    Serial.println(" ms");
    Serial.println("");
    
#endif
      
    sprintf(Line2, "Temp   %d", Temperature);
    sprintf(Line3, "Power  %d", AllowedPower);
    sprintf(Line4, "Akku   %d", RemainingAkku);

    /* measuring temperature via 1 Wire takes quit long ... do it only every 4th time */
    if (counter % 4 == 0 )
    {
      sensors.requestTemperatures(); // Send the command to get temperatures
      Temperature = sensors.getTempCByIndex(0);      

    }
    /* delay is only necessary when 1 wire temperature measuring was not performed */
    else  delay(MeasuringDelayIntervall);
    
    
    #if DEBUG == true
      if (RemainingAkku > 0 ) RemainingAkku--;
    #endif

    clearOld(LastTemperature,Temperature,1);
    
    clearOld(LastAllowedPower,AllowedPower,2);

    clearOld(LastRemainingAkku,RemainingAkku,3);
   
    counter = counter + 1;   
}

void clearOld(int OldValue, int NewValue,int LineNr)
{
    if (NewValue != OldValue )
    {
      if (NewValue < 10)  oled.setCursor(87,LineNr * 2);
      else
        if (NewValue < 100 )  oled.setCursor(98,LineNr * 2);
      
      oled.println("   ");
    }  
  
}
