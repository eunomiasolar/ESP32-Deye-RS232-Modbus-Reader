
#include "ModbusMaster232.h"
#include <HardwareSerial.h>

/*!

I wanted to read data from my Deye Inverter (Moddle SUN-5K-SG03LP1-EU)
but my model do not have the RS486 plug fitted as shown in the Deye 
manual that mention it is "available on SOME modles". 

So RS232 is the only option. I downloaded 3 programs for ESP32 that claim 
to read RS232 data, but none worked om my inverter. The Modbus address
they use are not valid/available on my inverter RS232 port.

I could not find any documentation on the Internet on Deye RS232 Modbus
protocol. (If you have it please send to "EunomiaSolar" at gmail.)

I spend a whole week experimenting with an ESP32 and this program to
search for the data I needed on my inverter.I found 9600 Baud, 8 bits, 
no parity, 1 stop bit works fine.

This program will read all Deye Modbus registers from 0 to 0X200 and
check what register is changing in value.

It will then list the values read from each changing register so that
you can deduct what value is stores in this register

This allow you to find the address of a regester that you are intrested
in. Just run it for +- 5 min and note how the value on your Deye inverter
changes, then search for the same changes in the output below.

    Sample output:

        New CHANGER at 0054
        New chang at 0055
        New CHANGER at 0057
        New chang at 0060
        New CHANGER at 00B6
        New chang at 00B7
        New chang at 00B8
        New chang at 00B9
        New chang at 00BA
        New chang at 00BB
        New chang at 00BE
        New chang at 00BF
        0X0000 (0) : 3,3,3,3,3,3,3,3,
        0X0001 (0) : 1,1,1,1,1,1,1,1,
        0X0002 (0) : 513,513,513,513,513,513,513,513,
        0X0003 (0) : 12851,12851,12851,12851,12851,12851,12851,12851,
        0X0004 (0) : 12338,12338,12338,12338,12338,12338,12338,12338,
        0X0005 (0) : 12601,12601,12601,12601,12601,12601,12601,12601,
        0X0006 (0) : 13876,13876,13876,13876,13876,13876,13876,13876,
        0X0007 (0) : 13880,13880,13880,13880,13880,13880,13880,13880,
        0X0009 (0) : 33024,33024,33024,33024,33024,33024,33024,33024,
        0X000B (0) : 5397,5397,5397,5397,5397,5397,5397,5397,

  We're using a MAX232 - compatible RS232 Transceiver.
  Rx/Tx is hooked up to the hardware serial port at 'HwSerial2'.
  The Data Enable and Receiver Enable pins are hooked up as follows:

 * There are three serial ports on the ESP known as U0UXD, U1UXD and U2UXD.
 * 
 * U0UXD is used to communicate with the ESP32 for programming and during reset/boot.
 * U1UXD is unused and can be used for your projects. Some boards use this port for SPI Flash access though
 * U2UXD is unused and can be used for your projects.
 *   
 *   UART    RX IO   TX IO   CTS     RTS
 *   UART0   GPIO3   GPIO1   N/A     N/A
 *   UART1   GPIO9   GPIO10  GPIO6   GPIO11
 *   UART2   GPIO16  GPIO17  GPIO8   GPIO7
 *   
 * You can assign uart devices to any I/O pins you prefer.
 * #define RXD2 = 21;
 * #define TXD2 = 22;
 * Serial1.begin(9600, SERIAL_8N1, gps_rx, gps_tx);
 * 
 *   HardwareSerial Serial2(2);
 *   ...
 *  Serial2.begin(9600,SERIAL_8N1,33,32); //works
 * 
 *  Bits per Byte
 *    1 start bit
 *    8 data bits, least significant bit sent first
 *    1 bit for even / odd parity-no bit for no parity
 *    1 stop bit if parity is used-2 bits if no parity
 *
*/
HardwareSerial HwSerial2(2);

#define RXD2 16
#define TXD2 17

int Debug = 0;

int BlocksStart = 0X0000;
int BlocksEnd   = 0X0200;

#define START_ADDR 0X0000
#define END_ADDR   0x1F00

////////////////////////////
// Change detector
///////////////////////////
unsigned short Buffer[END_ADDR - START_ADDR + 16];

#define MAX_CHANGERS 100
#define MAX_CHANGES  50
unsigned short Changes[MAX_CHANGERS * MAX_CHANGES];
unsigned short ChangeCnts[MAX_CHANGERS];
unsigned short ChangeAddr[MAX_CHANGERS];
unsigned short ChangerCnt = 0;

// instantiate ModbusMaster232 object
ModbusMaster232 node;

void preTransmission()
{
}

void postTransmission()
{
}

OnReceiveErrorCb OnReceiveError(hardwareSerial_error_t Err) 
{
static int Errors[7] = {0,0,0,0,0,0,0};
static int Cnt = 0;

  Errors[Err]++;
  if(Cnt++ >= 50)
  {
    Cnt = 0;
    Serial.printf("\nSerial ERRORS %d %d %d %d %d %d\n", 
          Errors[0],
          Errors[1],
          Errors[2],
          Errors[3],
          Errors[4],
          Errors[5]);
  }
  return(0);
}

void setup()
{
  // Coms communication runs at 115200 baud
  Serial.begin(115200);

  // Note the format for setting a serial port is as follows: 
  //    HwSerial2.begin(baud-rate, protocol, RX pin, TX pin);
  //     1 stop bit if parity is used-2 bits if no parity
  HwSerial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  HwSerial2.setRxBufferSize(1024);
  HwSerial2.onReceiveError(OnReceiveError); 

  Serial.println("Serial Txd is on pin: "+String(TXD2));
  Serial.println("Serial Rxd is on pin: "+String(RXD2));
  // Modbus 
  node.begin(1, HwSerial2);
  // Callbacks allow us to configure the RS232 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
}

////////////////////////////
// Change detector
void ListChanges(void)
{
  int i, j, k;

  for(i = 0; i < ChangerCnt; i++)
  {
    Serial.printf("0X%04X (%d) : ", ChangeAddr[i], Buffer[ChangeAddr[i] - START_ADDR]); 
    for(j = 0; j < ChangeCnts[i]; j++)
    {
      // unsigned short Changes[MAX_CHANGERS * MAX_CHANGES];
      Serial.printf("%d,", Changes[i * MAX_CHANGES + j]);
    }
    Serial.printf("\n");
  }
  Serial.printf("\n");
}

void loop()
{
  int i, j, k, Changed, dif, uniq, result;
  static int FirstTime = 1;

  if(FirstTime)
  {
    i = 0;
    while(i++ < 4)
    {
      delay(100);
      // 0X0000 -     3,    1,  513,12851,12338,12601,13876,13880,    
      result = node.readHoldingRegisters(0, 16);
      if (result == node.ku8MBSuccess)
      {
        k = node.getResponseBuffer(3);
        if(((k >> 8) < '0') || ((k >> 8) > '9'))
          continue;
        if(((k & 0xff) < '0') || ((k & 0xff) > '9'))
          continue;
    
        k = node.getResponseBuffer(4);
        if(((k >> 8) < '0') || ((k >> 8) > '9'))
          continue;
        if(((k & 0xff) < '0') || ((k & 0xff) > '9'))
          continue;
    
        k = node.getResponseBuffer(5);
        if(((k >> 8) < '0') || ((k >> 8) > '9'))
          continue;
        if(((k & 0xff) < '0') || ((k & 0xff) > '9'))
          continue;
    
        k = node.getResponseBuffer(6);
        if(((k >> 8) < '0') || ((k >> 8) > '9'))
          continue;
        if(((k & 0xff) < '0') || ((k & 0xff) > '9'))
          continue;
  
        k = node.getResponseBuffer(7);
        if(((k >> 8) < '0') || ((k >> 8) > '9'))
          continue;
        if(((k & 0xff) < '0') || ((k & 0xff) > '9'))
          continue;
    
        Serial.printf("Serial No. : %c%c%c%c%c%c%c%c%c%c\n",
             (node.getResponseBuffer(3) >> 8),
             (node.getResponseBuffer(3) & 0xff),
             (node.getResponseBuffer(4) >> 8),
             (node.getResponseBuffer(4) & 0xff),
             (node.getResponseBuffer(5) >> 8),
             (node.getResponseBuffer(5) & 0xff),
             (node.getResponseBuffer(6) >> 8),
             (node.getResponseBuffer(6) & 0xff),
             (node.getResponseBuffer(7) >> 8),
             (node.getResponseBuffer(7) & 0xff));
        break;
      }
      else
      {
        Serial.printf("Fail %d at SerialNo. Ret = %02X\n", i, result);
      }
    }
  }

////////////////////////////
// Change detector   V2.0
///////////////////////////

  if(FirstTime)
  {
      memset(&ChangeCnts[0], 0, sizeof(ChangeCnts));
      memset(&ChangeAddr[0], 0, sizeof(ChangeAddr));
      memset(&Changes[0], 0, sizeof(Changes));
  }
  while(1)
  {
    if(FirstTime)
    {
      FirstTime = 0;
      for(i = BlocksStart; i < BlocksEnd; i += 16)
      {
        result = node.readHoldingRegisters(i, 16);
        if (result == node.ku8MBSuccess)
        {
          Serial.printf("Read at 0X%04X\n", i);
          for(j = 0; j < 16; j++)
            Buffer[(i - BlocksStart) + j] = node.getResponseBuffer(j);
        }
        else
        {
          Serial.printf("Fail at 0X%04X Ret = %02X\n", i, result);
        }
        delay(50);
      }
      //ChangerCnt = 0;
      delay(1000);
      Serial.printf("Initial read done at %d\n\n", i);
    }
    else
    {
      Changed = 0;
      for(i = BlocksStart; i < BlocksEnd; i += 16)
      {
        result = node.readHoldingRegisters(i, 16);
        if (result == node.ku8MBSuccess)
        {
          for(j = 0; j < 16; j++)
          {
            if(Buffer[(i - BlocksStart) + j] != node.getResponseBuffer(j))
            {
              for(k = 0; k < ChangerCnt; k++)
              {
                if(ChangeAddr[k] == i + j)
                  break;
              }
              if(k >= ChangerCnt) // A new changer
              {
                if(ChangerCnt < MAX_CHANGERS)
                { 
                  ChangeCnts[ChangerCnt] = 1;
                  ChangeAddr[ChangerCnt] = i + j;
                  // unsigned short Changes[MAX_CHANGERS * MAX_CHANGES];
                  Changes[ChangerCnt * MAX_CHANGES + 0] = node.getResponseBuffer(j);
                  Serial.printf("New CHANGER at %04X\n", ChangeAddr[ChangerCnt]);
                  ChangerCnt++;
                  Changed = 1;
                }
              }
              else               // Add a new change
              {
                if(ChangeCnts[k] < MAX_CHANGES)
                {
                  Serial.printf("New chang at %04X\n", ChangeAddr[k]);
                  Changes[k * MAX_CHANGES + ChangeCnts[k]] = node.getResponseBuffer(j);
                  ChangeCnts[k]++;
                  Changed = 1;
                }
              }
            }
          }
        }
        else
        {
          Serial.printf("Fail at 0X%04X read. Ret = %02X\n", i, result);
        }
        delay(50);
      }
      if(Changed)
         ListChanges();
      delay(1000);
    }
    break;
  }

  delay(5000);
}


