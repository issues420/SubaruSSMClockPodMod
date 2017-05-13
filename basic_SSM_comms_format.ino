#include <SoftwareSerial.h>
//#include <SPI.h>//uncomment this if you want to use SPI to control the LCD, but change softwareserial pins to not be 9-13
#include <Wire.h>
#include <Adafruit_SSD1306.h>

SoftwareSerial kLine = SoftwareSerial(10, 11); //Rx, Tx

//button to switch between modes
//consider adding two buttons to cycle up and down
#define buttonPin 2

//ECU variables
/*here are the addresses we want:
manifold pressure sensor: 0x0200EA,0x0200EB
A/F sensor #1:0x020952,0x020953
Coolant temperature: 0x000008
Battery Voltage: 0x00001C
Intake air temperature: 0x000012
*/
//lets make individual polling messages for each parameter so we can switch between them
//the requests include the 3-byte address of the data byte that we want to read from ECU memory
//msg format = {header,to,from,# data bytes,read type flag(?),continuous response request flag,addr part 1,addr part 2, addr part 3,..., checksum}
uint8_t pollECU_pressure[13] = {128, 16, 240, 8, 168, 0, 0x02, 0x00, 0xEA, 0x02, 0x00, 0xEB, 0x09}; //response [X,X]
uint8_t pollECU_AF[13] = {128, 16, 240, 8, 168, 0, 0x02, 0x09, 0x52, 0x02, 0x09, 0x53, 0xEB}; //response [X,X]
uint8_t pollECU_Coolant[10] = {128, 16, 240, 8, 168, 0, 0x00, 0x00, 0x08, 0x38}; //response [X,X]-> I combined both coolant meas requests; I think this gives redundant info, though
uint8_t pollECU_BattVolt[10] = {128, 16, 240, 5, 168, 0, 0x00, 0x00, 0x1C, 0x49}; //response [X]
uint8_t pollECU_IAT[10] = {128, 16, 240, 5, 168, 0, 0x00, 0x00, 0x12, 0x3F}; //response [X]

uint8_t ECUResponseBuffer[2] = {0, 0};//the longest number of bytes we get from the ECU in response to our queries is 2 bytes
uint8_t parameterSelect = 0;//an index to keep track of which parameter to poll

//display pins -> use the hardware I2C pins
#define OLED_RESET  8
Adafruit_SSD1306 display(OLED_RESET);
/*if you want to use hardware SPI pins, uncomment this section
//and change the software serial pins to not use 9-13
#define OLED_DC     6
#define OLED_CS     7
#define OLED_RESET  8
Adafruit_SSD1306 display(OLED_DC, OLED_RESET, OLED_CS);
*/

//logo to display upon startup
const unsigned char PROGMEM S204logo [] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x1F, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x07, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x1F, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0xFF,
0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0xE0, 0x00, 0x01, 0xFF,
0x03, 0xFF, 0xC0, 0x00, 0x06, 0x03, 0xFF, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xF0, 0x00, 0x07, 0xFF,
0x07, 0xFF, 0x80, 0x00, 0x06, 0x01, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0, 0x00, 0x3F, 0xFF,
0x0F, 0xFF, 0xFF, 0xFF, 0x04, 0x00, 0x00, 0x07, 0xF0, 0x1F, 0xE0, 0x0F, 0xF0, 0x00, 0xFE, 0xFE,
0x0F, 0xFF, 0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x1F, 0xC0, 0x0F, 0xF0, 0x03, 0xF0, 0xFE,
0x07, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0xFF, 0xE0, 0x3F, 0x80, 0x1F, 0xE0, 0x0F, 0x80, 0xFE,
0x03, 0xFF, 0xFF, 0xFF, 0xF8, 0x00, 0xFF, 0xFF, 0xC0, 0x3F, 0x80, 0x1F, 0xE0, 0x3E, 0x01, 0xFC,
0x01, 0xFF, 0xFF, 0xFF, 0xF8, 0x07, 0xFF, 0xFF, 0x80, 0x3F, 0x80, 0x3F, 0xE0, 0xFC, 0x01, 0xFC,
0x00, 0x00, 0x01, 0xFF, 0xF8, 0x0F, 0xFC, 0x00, 0x00, 0x7F, 0x00, 0x3F, 0xC1, 0xFC, 0x03, 0xFF,
0x20, 0x00, 0x00, 0xFF, 0xF8, 0x1F, 0xC0, 0x00, 0x00, 0x7F, 0x00, 0x7F, 0xC1, 0xFF, 0xFF, 0xFF,
0x3C, 0x00, 0x00, 0xFF, 0xF0, 0x1F, 0xC0, 0x00, 0x00, 0x7F, 0x00, 0x7F, 0x81, 0xFF, 0xFF, 0xFF,
0x7F, 0xFF, 0xFF, 0xFF, 0xE0, 0x1F, 0xFF, 0xFF, 0xC0, 0x7F, 0xFF, 0xFF, 0x00, 0x00, 0x17, 0xF8,
0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x0F, 0xFF, 0xFF, 0xC0, 0x7F, 0xFF, 0xFE, 0x00, 0x00, 0x07, 0xF0,
0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x07, 0xFF, 0xFF, 0x80, 0x3F, 0xFF, 0xF8, 0x00, 0x00, 0x07, 0xE0,
0xFF, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char PROGMEM STIlogo [] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0xF0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0xE0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF1, 0xC0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xE1, 0xC0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xC0, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x03, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x07, 0x0F, 0xFF, 0xFF, 0x8F, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x07, 0x0F, 0xFF, 0xFF, 0x8F, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x07, 0x0F, 0xFF, 0xFF, 0x8F, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x01, 0xE3, 0x8E, 0x0E, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0xF3, 0x8E, 0x0E, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x73, 0x8E, 0x1C, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xF8, 0x73, 0x8E, 0x3C, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xF8, 0x73, 0x8E, 0x38, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x73, 0x8E, 0x38, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x8E, 0x71, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xE3, 0x8E, 0xF1, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xE3, 0x8E, 0xE3, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xC3, 0x8E, 0xE3, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0xFF, 0x03, 0x8F, 0xC7, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void setup() {
  Serial.begin(57600); //for diagnostics
  kLine.begin(4800); //SSM uses 4800 8N1 baud rate
  pinMode(buttonPin,INPUT);//initialize the button

  display.begin(SSD1306_SWITCHCAPVCC);//start the display
  display.clearDisplay();//make sure it is clear
  display.drawBitmap(0, 0, S204logo, 128, 64, 1);//load the logo
  display.display();//display the logo
  delay(5000);
}

void loop() {
  
  //let's read the individual ECU parameters based on a 'parameterSelect' index
  
  //these are buffers for displayable data
  double parameter = 0;
  String unit = "";
  String title = "";
  bool displayFlag = false;//if we fail an ECU poll, we don't want to update the display

  //make sure the parameter we are polling for is correctly indexed
  boolean button = digitalRead(buttonPin);//read the state of the 'switch parameter' button-> pull to high voltage to switch state
  //then, adjust the parameter we plan to poll next
  if(button)
  {
    parameterSelect += 1;
    if(parameterSelect > 4)
    {
      parameterSelect = 0;
    }
  }
  else if(false)
  {
    //can add a second button to switch parameters the other direction
    parameterSelect -= 1;
    if(parameterSelect < 0)
    {
      parameterSelect = 4;
    }
  }

if(parameterSelect == 0)
{
  //pressure sensor
  if(serialCallSSM(kLine, pollECU_pressure, 13, ECUResponseBuffer, 2, 0) < 10)
  {
    //the pressure calc looks like: x*0.1333224 for units of kPa absolute, or (x-760)*0.001333224 for units of Bar relative to sea level
    uint16_t combBytes = 256*ECUResponseBuffer[0] + ECUResponseBuffer[1];//we must combine the ECU response bytes into one 16-bit word - the response bytes are big endian
    double pressure = combBytes * 0.1333224;//kPa
    Serial.print(F("Absolute pressure: "));
    Serial.println(pressure);
    //update the buffers for displayable data
    parameter = pressure;
    unit = F(" kPa");
    title = F("Manifold pressure");
    displayFlag = true;
  }
}
else if(parameterSelect == 1)
{
  //oxygen sensor
  if(serialCallSSM(kLine, pollECU_AF, 13, ECUResponseBuffer, 2, 0) < 10)
  {
    //the Air/Fuel ratio calc looks like: x*0.00179443359375 for units of A/F ratio
    uint16_t combBytes = 256*ECUResponseBuffer[0] + ECUResponseBuffer[1];
    double AFR = combBytes * 0.00179443359375;
    Serial.print(F("Air/Fuel ratio: "));
    Serial.println(AFR);
    //update the buffers for displayable data
    parameter = AFR;
    unit = F("");
    title = F("Air/Fuel ratio");
    displayFlag = true;
  }
}
else if(parameterSelect == 2)
{
  //Coolant temp sensor - we polled two addresses to get the same info (same calculation, even)-> I ignored one of them
  if(serialCallSSM(kLine, pollECU_Coolant, 10, ECUResponseBuffer, 2, 0) < 10)
  {
    //the coolant temp calc looks like: x-40 for units of Deg C
    double coolantTemp = ECUResponseBuffer[0] - 40.0;
    Serial.print(F("Coolant temperature: "));
    Serial.println(coolantTemp);
    //update the buffers for displayable data
    parameter = coolantTemp;
    unit = F("degC");
    title = F("Coolant temp.");
    displayFlag = true;
  }
}
else if(parameterSelect == 3)
{
  //battery voltage
  if(serialCallSSM(kLine, pollECU_BattVolt, 10, ECUResponseBuffer, 2, 0) < 10)
  {
    //the battery voltage calc looks like: x*8/100 for units of Volts
    double volts = ECUResponseBuffer[0] * 8.00;
    volts = volts/100.00;
    Serial.print(F("Battery Voltage: "));
    Serial.println(volts);
    //update the buffers for displayable data
    parameter = volts;
    unit = F(" V");
    title = F("Battery voltage");
    displayFlag = true;  
  }
}
else if(parameterSelect == 4)
{
  //intake air temperature
  if(serialCallSSM(kLine, pollECU_IAT, 10, ECUResponseBuffer, 2, 0) < 10)
  {
    //the intake air temperature calc looks like: x-40 for units of Deg C
    double IAT = ECUResponseBuffer[0] - 40.0;
    Serial.print(F("Intake air temperature: "));
    Serial.println(IAT);
    //update the buffers for displayable data
    parameter = IAT;
    unit = F("degC");
    title = F("Intake air temp.");
    displayFlag = true;    
  } 
}
  Serial.println();
 
  //now that we've read the parameters we're interested in, lets display one
  if(displayFlag)
  {
    writeToDisplay(parameter,unit,title);
  }
  else
  {
    //in case we want to do something special when comms stops, put it here
    //this could be at engine off.
    display.clearDisplay();//make sure it is clear
    display.drawBitmap(0, 0, STIlogo, 128, 64, 1);//load the logo
    display.display();//display the logo
  }
}


boolean readSSM(SoftwareSerial &serialPort, uint8_t *dataBuffer, uint16_t bufferSize)
{
  //read in an SSM serial response packet, place the data bytes in the data buffer
  //and return a boolean value indicating whether the checksum failed
  uint16_t timeout = 100;//100 millisecond timeout for response
  uint32_t timerstart = millis();//start the timer
  boolean notFinished = true;//a flag to indicate that we are done
  boolean checksumSuccess = false;
  uint8_t dataSize = 0;
  uint8_t calcSum = 0;
  uint8_t packetIndex = 0;

  Serial.print(F("Read Packet: [ "));
  while(millis() - timerstart <= timeout && notFinished)
  {
    if(serialPort.available() > 0)
    {
      uint8_t data = serialPort.read();
      Serial.print(data);
      Serial.print(F(" "));//if not printing data, be sure to delay to allow for more bytes to come in
      if(packetIndex == 0 && data == 128)
      {
        //0x80 or 128 marks the beginning of a packet
        packetIndex += 1;
        calcSum += data;
      }
      else if(packetIndex == 1 && data == 240)
      {
        //this byte indicates that the message recipient is the 'Diagnostic tool'
        packetIndex += 1;
        calcSum += data;
      }
      else if(packetIndex == 2 && data == 16)
      {
        //this byte indicates that the message sender is the ECU
        packetIndex += 1;
        calcSum += data;
      }
      else if(packetIndex == 3)
      {
        //this byte indicates the number of data bytes which follow
        dataSize = data;
        packetIndex += 1;
        calcSum += data;
      }
      else if(packetIndex == 4)
      {
        //I don't know what this byte is for
        packetIndex += 1;
        calcSum += data;
      }
      else if(packetIndex > 4 && packetIndex < (dataSize+4))
      {
        //this byte is data
        if(packetIndex - 5 < bufferSize)//make sure it fits into the buffer
        {
          dataBuffer[packetIndex - 5] = data;
        }
        packetIndex += 1;
        calcSum += data;
      }
      else if(packetIndex == dataSize + 4)
      {
        //this is the checksum byte
        if(data == calcSum)
        {
          //checksum was successful
          checksumSuccess = true;
        }
        else
        {
          Serial.print(F("Checksum fail "));
        }
        notFinished = false;//we're done now
        break;
      }
      timerstart = millis();//reset the timeout if we're not finished
    }
  }
  Serial.println(F("]"));
  if(notFinished)
  {
    Serial.println(F("Comm. timeout"));
  }
  return checksumSuccess;
}

//writes data over the software serial port
void writeSSM(SoftwareSerial &serialPort, uint8_t *dataBuffer, uint8_t bufferSize) {
  uint8_t sum = 0;
  Serial.print(F("Sending packet: [ "));
  for (uint8_t x = 0; x < bufferSize; x++) {
    if(x == bufferSize - 1 && sum != dataBuffer[x])
    {
      dataBuffer[x] = sum;//fix the checksum in the data buffer
    }
    else
    {
      sum += dataBuffer[x];//build the checksum
    }
    serialPort.write(dataBuffer[x]);
    Serial.print(dataBuffer[x]);
    Serial.print(F(" "));
  }
  Serial.println(F("]"));
}

uint8_t serialCallSSM(SoftwareSerial &serialPort, uint8_t *sendDataBuffer, uint16_t sendBufferSize, uint8_t *receiveDataBuffer, uint16_t receiveBufferSize, uint8_t attempts)
{
  //this function performs the call and response routine for 
  //exchanging serial data with the Subaru ECU via SSM
  //it sends a message and awaits a response, resending the 
  //message if we reach timeout or if the checksum failed
  //it stops trying the data exchange if 10 failures occur and 
  //it outputs the number of attempts it failed to send data
  //input: handle to software serial pins
  //  buffer with outgoing data and length of buffer
  //  buffer for received data (not the whole packet), and length of buffer
  //  the initial value for a counter of failed attempts

  writeSSM(serialPort, sendDataBuffer, sendBufferSize);//send the message
  boolean success = readSSM(serialPort, receiveDataBuffer, receiveBufferSize);//receive the response
  uint8_t newAttempt = 0;
  
  if(!success && attempts < 10)
  {
    Serial.println(F("Packet exchange failed, trying again... "));
    newAttempt = serialCallSSM(serialPort, sendDataBuffer, sendBufferSize, receiveDataBuffer, receiveBufferSize, attempts + 1);
  }
  newAttempt += attempts;
  
  return newAttempt;
}


void writeToDisplay(double parameter, String unit, String title)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(title);
  display.setTextSize(3);
  display.print(parameter);
  display.setTextSize(2);
  display.println(unit);
  display.display();
}

