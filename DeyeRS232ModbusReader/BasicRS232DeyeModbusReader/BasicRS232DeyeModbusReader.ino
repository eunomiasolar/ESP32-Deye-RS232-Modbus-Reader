
#include "ModbusMaster232.h"
#include <HardwareSerial.h>

/*!
  We're using a MAX232 - compatible RS232 Transceiver.
  Rx/Tx is hooked up to the hardware serial port at 'HwSerial2'.
  The Data Enable and Receiver Enable pins are hooked up as follows:

This will print some of Dey registers read from RS323 port over Modbus protocol
The standard modbuss library was modified and PollModbus() function added
to allow the main loop() to run faster (No long waits to read long RS232 
modbus reply packets in main loop())

These are the important registers I found:
  1) Solar pannel Volt Amp and Wats for the two strings
  2) Battery Voltages for (1) Charging and (2) as read from BMS over CAN bus
  3) POWER in Watt delivered on 220V output
  4) Battery charge persentage
  5) Battery Discharge Amps (negative for charging)
    
    Example output
        PV1 V     0X006D:   199.40
        PV1 A     0X006E:     8.50
        PV1 W     0X00BA:  1664.00 
        PV2 V     0X006F:   196.70
        PV2 A     0X0070:     8.60
        PV2 W     0X00BB:  1657.00
        BAT BMS V 0X007E:    54.71   
        BAT CH  V 0X00B7:    54.41
        POW1    W 0X00AD:   514.00
        GRID    V 0X008C:    5.00
        GRID    A 0X00A0:    0.00
        BAT %     0X00B8:    67.00
        DISCHG  A 0X00BF:   -48.58 
        BAT WATS             40.00

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
 * gps_rx=21;
 * gps_tx=22;
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
 *    Error Check Field
 *    Cyclical Redundancy Check (CRC) 
 *
*/
HardwareSerial HwSerial2(2);

#define RXD2 16
#define TXD2 17

int Debug = 0;

#define STATE_REQUEST_6D 1
#define STATE_WAIT_6D    2
#define STATE_REQUEST_A0 3
#define STATE_WAIT_A0    4
#define STATE_IDLE_1     5
#define STATE_IDLE_2     6

////////////////////////////
// Display Registers
////////////////////////////

typedef struct RS232Registers{
  int         Reg_no;
  const char *Name;
  short       Value;
  float       Factor;
}RS232Registers;

struct RS232Registers Regs[]{
  {0X006D, "PV1 V    ", 0, 0.1 },     // 109  "PV1 V    " Total : BF - 6D = 82 registers
  {0X006E, "PV1 A    ", 0, 0.1 },     // 110  "PV1 A    "
  {0X00BA, "PV1 W    ", 0, 1.0 },     // 186  "PV1 W    "
  {0X006F, "PV2 V    ", 0, 0.1 },     // 111  "PV2 V    "
  {0X0070, "PV2 A    ", 0, 0.1 },     // 112 "PV2 A    "
  {0X00BB, "PV2 W    ", 0, 1.0 },     // 187  "PV2 W    "
  {0X007E, "BAT BMS V", 0, 0.01},     // 126  "BAT BMS V" Block1 : 8C - 6D = 31 registers
  {0X00B7, "BAT CHG V", 0, 0.01},     // 183  "BAT CH  V"
  {0X00AD, "POWER   W", 0, 1.0 },     // 173  "POW1 W   " Block2 : BF - A0 = 31 registers
  {0X00B8, "BAT %    ", 0, 1.0 },     // 184  "BAT %    "
  {0X00BF, "DISCHG  A", 0, 0.01},     // 191  "BAT Discharge A "
  {0X008C, "GRID    V", 0, 0.1 },     // 140  "GRID Voltage  V "
  {0X00A0, "GRID    A", 0, 0.01},     // 160  "GRID Current  A "
  {0,      NULL,        0, 0.0 }
};

#define BLOCK_6D_START      0x6D
#define BLOCK_6D_LENGTH     32
#define BLOCK_A0_START      0xA0
#define BLOCK_A0_LENGTH     32
 
#define REG_6D_PV1_V      0
#define REG_6E_PV1_A      1
#define REG_BA_PV1_W      2
#define REG_6F_PV2_V      3
#define REG_70_PV2_A      4
#define REG_BB_PV2_W      5
#define REG_7E_BAT_BMS_V  6
#define REG_B7_BAT_CH_V   7
#define REG_AD_POW1_W     8
#define REG_B8_BAT_PERS   9
#define REG_BF_BAT_DIS_C 10
#define REG_8C_GRID_V    11
#define REG_A0_GRID_A    12

// instantiate ModbusMaster232 object
ModbusMaster232 node;

                          
void preTransmission()
{
}

void postTransmission()
{
}

OnReceiveCb OnReceiveFunction(void)
{
  node.RxCallback();
  return(0);
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

  Serial.printf("ESP Flash size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));

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

int ModbusPollLoop(void)
{
static int Loop = 0;
static unsigned int Start = millis();
static int State = STATE_REQUEST_6D;
int result;

  switch(State)
  {
    case STATE_REQUEST_6D:
      node.readPollHoldingRegisters(BLOCK_6D_START, BLOCK_6D_LENGTH);
      State = STATE_WAIT_6D;
      return(node.ku8MBPollWaitRx);

    case STATE_WAIT_6D:
      result = node.PollModbus();
      if(result == node.ku8MBSuccess)
      {
        if(Debug)Serial.printf("Got 6D\n");
        Regs[REG_6D_PV1_V    ].Value = node.getResponseBuffer(Regs[REG_6D_PV1_V    ].Reg_no - BLOCK_6D_START);
        Regs[REG_6E_PV1_A    ].Value = node.getResponseBuffer(Regs[REG_6E_PV1_A    ].Reg_no - BLOCK_6D_START);
        Regs[REG_6F_PV2_V    ].Value = node.getResponseBuffer(Regs[REG_6F_PV2_V    ].Reg_no - BLOCK_6D_START);
        Regs[REG_70_PV2_A    ].Value = node.getResponseBuffer(Regs[REG_70_PV2_A    ].Reg_no - BLOCK_6D_START);
        Regs[REG_7E_BAT_BMS_V].Value = node.getResponseBuffer(Regs[REG_7E_BAT_BMS_V].Reg_no - BLOCK_6D_START);
        Regs[REG_8C_GRID_V   ].Value = node.getResponseBuffer(Regs[REG_8C_GRID_V   ].Reg_no - BLOCK_6D_START);
        State = STATE_IDLE_1;
      }
      else if(!((result == node.ku8MBPollWaitRx) || (result == node.ku8MBPollWaitRest))) 
      {
        State = STATE_IDLE_1;       // There was some other error
      }
      result = node.ku8MBPollWaitRx;    // We wait till all requests are done
      return(result);

    case STATE_IDLE_1:
      if((Start + 1000) <= millis())
      {
        Start = millis();
        State = STATE_REQUEST_A0;
      }
      return(node.ku8MBPollWaitRx);

    case STATE_REQUEST_A0:
      node.readPollHoldingRegisters(BLOCK_A0_START, BLOCK_A0_LENGTH);
      State = STATE_WAIT_A0;
      return(node.ku8MBPollWaitRx);

    case STATE_WAIT_A0:
      result = node.PollModbus();
      if (result == node.ku8MBSuccess)
      {
        if(Debug)Serial.printf("Got A0\n");
        Regs[REG_AD_POW1_W   ].Value = node.getResponseBuffer(Regs[REG_AD_POW1_W   ].Reg_no - BLOCK_A0_START);
        Regs[REG_B7_BAT_CH_V ].Value = node.getResponseBuffer(Regs[REG_B7_BAT_CH_V ].Reg_no - BLOCK_A0_START);
        Regs[REG_B8_BAT_PERS ].Value = node.getResponseBuffer(Regs[REG_B8_BAT_PERS ].Reg_no - BLOCK_A0_START);
        Regs[REG_BF_BAT_DIS_C].Value = node.getResponseBuffer(Regs[REG_BF_BAT_DIS_C].Reg_no - BLOCK_A0_START);
        Regs[REG_BA_PV1_W    ].Value = node.getResponseBuffer(Regs[REG_BA_PV1_W    ].Reg_no - BLOCK_A0_START);
        Regs[REG_BB_PV2_W    ].Value = node.getResponseBuffer(Regs[REG_BB_PV2_W    ].Reg_no - BLOCK_A0_START);
        Regs[REG_A0_GRID_A   ].Value = node.getResponseBuffer(Regs[REG_A0_GRID_A   ].Reg_no - BLOCK_A0_START);
        State = STATE_IDLE_2;
        Serial.printf("\nLoop %d for %dms\n", Loop++, millis() - Start);
      }
      else if(!((result == node.ku8MBPollWaitRx) || (result == node.ku8MBPollWaitRest))) 
      {
        State = STATE_IDLE_2;       // There was some other error
      }
      return(result);

    case STATE_IDLE_2:
      if((Start + 1000) <= millis())
      {
        Start = millis();
        State = STATE_REQUEST_6D;
      }
      return(node.ku8MBPollWaitRx);
  }
  return(-1);
}

void loop()
{
int i, k, result, Last = millis();
static int FirstTime = 1;
static int Cnt = 0;
static float ChgAH = 0;
static float DisAH = 0;
static float PV_W  = 0;
static float ChgTotalAH = 0;
static float DisTotalAH = 0;
static float PVtotalWH  = 0;
 
////////////////////////////
// Serial Number
////////////////////////////
  if(FirstTime)
  {
    FirstTime = 0;
    i = 0;
    while(i++ < 4)
    {
      delay(50);
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
    HwSerial2.onReceive(OnReceiveFunction, 0);  // Enable the UART RX upcall
  }

////////////////////////////
// Display Registers
////////////////////////////
  result = ModbusPollLoop();
  if(result == node.ku8MBSuccess)
  {
    i = 0;
    while(Regs[i].Name)
    {
      Serial.printf("%s 0X%04X: %8.2f\n", Regs[i].Name, Regs[i].Reg_no, (float)Regs[i].Value * Regs[i].Factor);
      i++;
    }
    Serial.printf("BAT Charging  AH %8.2f\n", ChgTotalAH);
    Serial.printf("BAT Discharge AH %8.2f\n", DisTotalAH);
    Serial.printf("PV Energy     WH %8.2f\n", PVtotalWH);
    i = 0;
    while(i < 2000)       // Wait 2 seconds after prints
    {
      ModbusPollLoop();
      delay(2);
      i += 2;
    }
  }
  else
  {
    if(!((result == node.ku8MBPollWaitRx) || (result == node.ku8MBPollWaitRest))) 
       Serial.printf("Fail %02X reading %04X len %d\n", result, BLOCK_A0_START, BLOCK_A0_LENGTH);
  }

  if((Last + 2000) <  millis())        // Every 2 seconds
  {
    Last = millis();
    if((float)Regs[REG_BF_BAT_DIS_C].Value * Regs[REG_BF_BAT_DIS_C].Factor > 0) // Discharging
    {
      DisAH += (float)Regs[REG_BF_BAT_DIS_C].Value * Regs[REG_BF_BAT_DIS_C].Factor;
    }
    else                                                                        // Charging
    {
      ChgAH -= (float)Regs[REG_BF_BAT_DIS_C].Value * Regs[REG_BF_BAT_DIS_C].Factor;
    }
    PV_W += (float)Regs[REG_BA_PV1_W].Value * Regs[REG_BA_PV1_W].Factor +
            (float)Regs[REG_BB_PV2_W].Value * Regs[REG_BB_PV2_W].Factor;
    if(++Cnt == 30)  // one min
    {
      Cnt = 0;
      DisTotalAH += DisAH / (30 * 60);  // Avarage over 1 min and scale to hour     
      ChgTotalAH += ChgAH / (30 * 60);  // Avarage over 1 min and scale to hour     
      PVtotalWH  += PV_W  / (30 * 60);  // Avarage over 1 min and scale to hour     
    }
  }
  delay(1);
}


