
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "FS.h"
#include <LITTLEFS.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include "ESP32_FTPClient.h"
#include "ESP32_Update.h"
#include "ESP32Ping.h"
#include "ESPTelnet.h"
#include "time.h"
#include <ESP32Time.h>
#include <esp_task_wdt.h>
#include "ModbusMaster232.h"
#include <HardwareSerial.h>

/*!
  We're using a MAX232 - compatible RS232 Transceiver.
  Rx/Tx is hooked up to the hardware serial port at 'HwSerial2'.
  The Data Enable and Receiver Enable pins are hooked up as follows:
*/
/*
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

//120 seconds WDT
#define WDT_TIMEOUT 120

#define VERSION "BETA 0.01"

#define MAX_LOAD      6
#define HIGH_LOAD     4
#define MED_LOAD      3
#define LOW_LOAD      2

#define HIGH_LED_ON   2100
#define MED_LED_ON    1400
#define LOW_LED_ON     800
#define NONE_LED_ON    200
#define START_LED_ON  1000

#define HIGH_LED_OFF   200
#define MED_LED_OFF    800
#define LOW_LED_OFF   1400
#define NONE_LED_OFF  2100
#define START_LED_OFF 1000
  
#define FORMAT_LITTLEFS_IF_FAILED true

//ESP32Time rtc;
ESP32Time rtc(7200);  // offset in seconds GMT+1

int Debug = 4;

int LogTimeHour, LogTimeMin;
char LogOnTime[11]  = "17H00";
int LogIntSec = 10;
char LogIntervalSec[11];

#define MAX_NAME_LIST 44
char FileNameList[MAX_NAME_LIST][43];

int FtpSendingSummery = 0;
char SummeryFileName[33] = "xxx";
char SummeryBuffer[43];
char NewFileName[33]     = "";
char StoreFileName[33]   = "";
File SumFile;
File LogFile;
File SummFile; 
File SummReadFile; 
File ReadFile;
int LogFileSize = 0;
int FtpSendDone = 1;
int LogFileDay  = 33;
int LoggingOn   = 0;
time_t LogStartTime;
time_t FtpFailTime = 0;
int SumFileSeq = 0;

char Username[22] = "modbus";
char Password[22] = "monitor";
WiFiClient DeadDummy;
int LogedIn = 0;
int UserFound = 0;
int UserLogedIn = 0;
IPAddress LogedInIP(0, 0, 0, 0);
IPAddress IPDummy(0, 0, 0, 0);
IPAddress NewIP;
time_t LogedInIPtimeout = 0;
char SerialNo[11] = "          ";
float ChgTotalAH = 0;
float DisTotalAH = 0;
float PVtotalWH  = 0;

unsigned int ShowMBregs   = 0;
unsigned int ShowRegValue = 0;

int ForcedLogFile = 0;

const int BlueLEDpin      = 2;  // Light power

// Set web server port number to 80
WiFiServer HttpServer(80);

// Variable to store the HTTP request
String Header;

// Auxiliar variables to store the current output state
int    RSSIval = 1;
int MaxPower = 0;

struct tm Now;
char NtpServer[44] = "pool.ntp.org";
long  GmtOffset_sec = 2 * 60 * 60;
int   DaylightOffset_sec = 0;

int Rebooted = 1;
char Empty[2];
#define EMPTY (const char *)&Empty[0]

// Current time
unsigned long CurrentTime = millis();
// Previous time
unsigned long PreviousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long TimeoutTime = 2000;
int Uptime = 0;

// Replace with your network credentials
char WiFiSSID[22];      
char WiFiPassword[22]; 

#define HOSTNAME "MBmonitor"
const uint8_t Ap_ip[4]        = {192,168,0  ,111}; 
const uint8_t Ap_gateway[4]   = {192,168,0  ,2  };
const uint8_t Ap_subnet[4]    = {255,255,255,0  };

// Set your Static IP address
IPAddress Local_IP;                    
// Set your Gateway IP address
IPAddress Gateway;                    

IPAddress Subnet;                   
IPAddress PrimaryDNS;              
IPAddress SecondaryDNS;           
char SiteName[22] = "";                 // MBmonitor

int CredentialsNeeded = 1;

char BootMessage[44];

IPAddress ClientInIP(0, 0, 0, 0);
#define MAX_CLIENTS  20
time_t timeNow = 0;
time_t ResetRequierd = 0;

const uint8_t *ResetMessage = (const unsigned char *) "user modbus\n\rpass monitor\n\rreboot\n\r";

char ftp_server[17] = "10.0.0.118"; 
char ftp_user[14]   = "ftpuser";   
char ftp_pass[14]   = "esp32";    

// you can pass a FTP timeout and debbug mode on the last 2 arguments : uint16_t _timeout = 10000, uint8_t _verbose = 1);
ESP32_FTPClient ftp (ftp_server,ftp_user,ftp_pass, 5000, 2);

int OtaFtpGetDone = 0;       
int OtaBytesRx = 0;
int OtaFileSize;
      
uint8_t OtaBuffer[1500];

#define MAX_EVENT  30

char EventLog[MAX_EVENT][60];
int  CurrentEvent = 0;

ESPTelnet telnet;
uint16_t  TelnetPort = 9000;
int TelnetActive = 0;

char PrtBuff[222];

__NOINIT_ATTR uint32_t DataNoInitCount;
RTC_NOINIT_ATTR uint32_t RtcNoInitCount;

#define MAX_MSG 70

typedef struct NoInitMessages{
  int len;
  int sum;
  char mesg[MAX_MSG];
} NoInitMessages;
RTC_NOINIT_ATTR struct NoInitMessages NoInitMess[MAX_MSG];
RTC_NOINIT_ATTR int NextMessPos;

#define STATE_REQUEST_6D 1
#define STATE_WAIT_6D    2
#define STATE_REQUEST_A0 3
#define STATE_WAIT_A0    4
#define STATE_IDLE_1     5
#define STATE_IDLE_2     6
#define STATE_IDLE_3     7
#define STATE_REQ_SHOW   8
#define STATE_WAIT_SHOW  9
9
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
  // HEX     Name      Val Factor   Decimal  Discription
  {0X006D, "PV1 V    ", 0, 0.1 }, // 109  Solar Pannel String 1 Volt    
  {0X006E, "PV1 A    ", 0, 0.1 }, // 110  Solar Pannel String 1 Amp    
  {0X00BA, "PV1 W    ", 0, 1.0 }, // 186  Solar Pannel String 1 Watt    
  {0X006F, "PV2 V    ", 0, 0.1 }, // 111  Solar Pannel String 2 Volt    
  {0X0070, "PV2 A    ", 0, 0.1 }, // 112  Solar Pannel String 2 Amp    
  {0X00BB, "PV2 W    ", 0, 1.0 }, // 187  Solar Pannel String 2 Watt    
  {0X007E, "BAT BMS V", 0, 0.01}, // 126  Battery Voltage from BMS
  {0X00B7, "BAT CHG V", 0, 0.01}, // 183  Battery Charging Voltage 
  {0X00AD, "POWER   W", 0, 1.0 }, // 173  Sytem Wats power deliverd to home
  {0X00B8, "BAT %    ", 0, 1.0 }, // 184  % Charge in battery
  {0X00BF, "DISCHG  A", 0, 0.01}, // 191  BAT Discharge A "
  {0X008C, "GRID    V", 0, 0.1 }, // 140  GRID Input Voltage  V "
  {0X00A0, "GRID    A", 0, 0.01}, // 160  GRID Input Current  A "
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

int DebugPrintf(const char *format, ...) 
{
  int res; 
  va_list ptr;
  va_start(ptr, format);
  res = vsprintf(PrtBuff, format, ptr);
  if(TelnetActive)
  {
    res = telnet.printf("%s", PrtBuff);
  }
  else
  {
    res = Serial.printf("%s", PrtBuff);
  }
  va_end(ptr);
  return res;
}

void OtaProgressCallBack(size_t currSize, size_t totalSize) 
{
  DebugPrintf("Callback:  Update process at %d of %d bytes...\n", currSize, totalSize);
}
        
int FtpReadFileUpCall(unsigned char * Buffer, int Len)
{
int count;

  if(FtpSendingSummery)
  {
    count = SummReadFile.read(Buffer, Len); 
    DebugPrintf("Read %d of %d\n", count, Len);
    if(count < Len)
      FtpSendDone = 1;
    return(count);
  }
  else
  {
    count = ReadFile.read(Buffer, Len); 
    DebugPrintf("Read %d of %d\n", count, Len);
    if(count < Len)
      FtpSendDone = 1;
    return(count);
  }
}

int FtpWriteFileUpCall(unsigned char * Buffer, int Len)
{
int count;
  
  if(Len == 0)
  {
    DebugPrintf("Done at Total %d\n", OtaBytesRx);
    OtaFtpGetDone = 1;
    return(0);
  }
  count = Update.write(Buffer, Len); 
  if(count != Len)
  {
    DebugPrintf("WRITE Fail at count %d of %d\n", count, Len);
    OtaFtpGetDone = -1;
    return(-1);
  }
  OtaBytesRx += count;
  DebugPrintf("Count %d Total %d\n", count, OtaBytesRx);
  if(Len < 4096)
  {
    OtaFtpGetDone = 1;
  }
  return(count);
}

int LoadFirmwareFromFTP(void)
{
int len = 0;

  ftp.OpenConnection();
  ftp.InitFile("Type I");
  OtaFtpGetDone = 0;
  OtaBytesRx = 0;
    
  DebugPrintf("OTA from %s as %s with %s\n", ftp_server, ftp_user, ftp_pass);

  if((OtaFileSize = ftp.DownloadFileStart("MBfirmware.bin")) == 0)
    return(-3);
  DebugPrintf("File MBfirmware.bin size %d.\n", OtaFileSize);

  Update.onProgress(OtaProgressCallBack);
  Update.begin(OtaFileSize, U_FLASH); // U_SPIFFS);
    
  ftp.DownloadFile();
  DebugPrintf("File MBfirmware.bin downloaded.\n");
  if(OtaFtpGetDone == 0)
  {
    DebugPrintf("Failed to ftp file\n");
    OtaFtpGetDone = -1;
    return(-2);
  }
  ftp.CloseFile();
  ftp.CloseConnection();
  DebugPrintf("The uploade MBfirmware now stored in FLASH\n");
  if(Update.end(1))
  {
    DebugPrintf("Update finished!\n");
  }
  else
  {
    DebugPrintf("Update error : %s\n", Update.errorString());
    return(-1);
  }
  delay(2000);
  ESP.restart();
  return(1);
}

int writeBinFile(fs::FS &Fs, const char * Path, const uint8_t * Data, int Len)
{
int ret;

  DebugPrintf("Writing bin file: %s\n", Path);

  File file = Fs.open(Path, FILE_WRITE);
  if(!file)
  {
    DebugPrintf("Failed to open file for writing\n");
    return(-1);
  }
  ret = file.write(Data, Len);
  return(ret);
}

int writeFile(fs::FS &Fs, const char * Path, const char * Message)
{
int ret;

  DebugPrintf("Writing txt file: %s\n", Path);

  File file = Fs.open(Path, FILE_WRITE);
  if(!file)
  {
    DebugPrintf("Failed to open file for writing\n");
    return(-1);
  }
  if((ret = file.print(Message)) > 0)
  {
    DebugPrintf("File written %d bytes\n", ret);
  } 
  else 
  {
    DebugPrintf("Write failed\n");
    file.close();
    return(-1);
  }
  file.close();
  return(ret);
}

int deleteFile(fs::FS &Fs, const char * Path)
{
char FileName[33];

  DebugPrintf("Delete file: %s\n", Path);
  if(Path[0] != '/')
  {
    sprintf(FileName, "/%s", Path);
    Path = &FileName[0];
  }
  File file = Fs.open(Path);
  if(!file || file.isDirectory())
  {
    DebugPrintf("Failed to open file %s for deleting\n", Path);
    return(0);
  }
  file.close();
  if(Fs.remove(Path))
    return(1);
  else
    return(0);
}

int DoCatCmnd(fs::FS &Fs, const char * Path)
{

  File file = Fs.open(Path);
  if(!file || file.isDirectory())
  {
    DebugPrintf("Failed to open file %s for cat\n", Path);
    return(-1);
  }

  DebugPrintf("Read from file:\n");
  while(file.available())
  {
    DebugPrintf("%c", file.read());
  }
  file.close();
  DebugPrintf("\n");
  return(0);
}

int readFile(fs::FS &Fs, const char * Path, char * Message)
{
int i = 0;

  DebugPrintf("Reading file: %s\n", Path);

  File file = Fs.open(Path);
  if(!file || file.isDirectory())
  {
    DebugPrintf("Failed to open file %s for reading\n", Path);
    return(-1);
  }

  DebugPrintf("Read from file:\n");
  while(file.available())
  {
    Message[i++] = file.read();
  }
  file.close();
  return(i);
}

int readBinFile(fs::FS &Fs, const char *Path, uint8_t *Data, int Len)
{
int ret = 0;

  DebugPrintf("Reading file: %s\n", Path);

  File file = Fs.open(Path);
  if(!file || file.isDirectory())
  {
    DebugPrintf("Failed to open file %s for reading\n", Path);
    return(-1);
  }
  ret = file.read(Data, Len);
  DebugPrintf("Read: %d bytes\n", ret);
  file.close();
  return(ret);
}

void setupTelnet() 
{  
  // passing on functions for various telnet events
  telnet.onConnect(onTelnetConnect);
  telnet.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet.onReconnect(onTelnetReconnect);
  telnet.onDisconnect(onTelnetDisconnect);
  telnet.onInputReceived(onTelnetInput);
  telnet.setLineMode(true);

  DebugPrintf("Telnet: ");
  if(telnet.begin(TelnetPort)) 
  {
    DebugPrintf("running\n");
  } 
  else 
  {
    DebugPrintf("error. Will reboot...\n");
  }
}

// (optional) callback functions for telnet events
void onTelnetConnect(String ip) 
{
  Serial.print("Telnet: ");
  Serial.print(ip);
  Serial.print(" connected\n");
  
  telnet.println("\nLOAD SERVER: Welcome " + telnet.getIP());
  telnet.println("(Use ^] + q  to disconnect.)");

  TelnetActive = 1;
}

void onTelnetDisconnect(String ip) 
{
  Serial.print("Telnet: ");
  Serial.print(ip);
  Serial.print(" disconnected\n");
  TelnetActive = 0;
  LogedIn = 0;
}

void onTelnetReconnect(String ip) 
{
  Serial.print("Telnet: ");
  Serial.print(ip);
  Serial.print(" reconnected\n");
  TelnetActive = 1;
}

void onTelnetConnectionAttempt(String ip) 
{
  Serial.print("Telnet: ");
  Serial.print(ip);
  Serial.print(" tried to connect\n");
}

void DoResetRequierd(const char *Msg)
{ 
  DebugPrintf("Reset now : %s.\n", Msg);
  writeFile(LITTLEFS, "/BootMessage.txt", Msg); 
  LogEvent(LITTLEFS, Msg, EMPTY);
  ResetRequierd = timeNow + 6;
  NoInitMsg(Msg);
  LITTLEFS.end();
}


void CleanDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
char FileName[33];
int i = 0;

    DebugPrintf("Clean directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root)
    {
        DebugPrintf(" - failed to open directory\n");
        return;
    }
    if(!root.isDirectory())
    {
        DebugPrintf(" - not a directory\n");
        return;
    }

    File file = root.openNextFile();
    while(file)
    {
        if(file.isDirectory())
        {
            DebugPrintf("DIR : %s\n", file.name());
            if(levels)
            {
                // Recursive call to list subdirectories
                CleanDir(fs, file.path(), levels -1);
            }
        } 
        else 
        {
            // if((file.name()[0] == 'M') && (file.name()[1] == 'B'))
  
            // check for MBmon in filenames like "MBmonLog2026-05-19_16H25.csv"
            if(strncmp(file.name(), "MBmon", 5) == 0)
            {
              sprintf(FileNameList[i++], "/%s", file.name());
              if(i >= MAX_NAME_LIST)
                break;
              //  //if(fs.remove(file.name()))
              //  //if(fs.remove(FileName))
              //  if(deleteFile(LITTLEFS, (const char *)&FileName[0]))
              //  {
              //    DebugPrintf("    - File [%s] deleted successfully\n", FileName);
              //  } 
              //  else 
              //  {
              //    DebugPrintf("    - Delete [%s] failed\n", FileName);
              //  }
            }
        }
        file = root.openNextFile();
    }
}

void ListDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
int line = 0;
struct stat file_stat;
time_t lastWrite;

  DebugPrintf("Listing directory: %s\n", dirname);
  DebugPrintf("          File Name          Size(bytes)  Age(days)             File Name          Size(bytes)  Age(days)\n");

  File root = fs.open(dirname);
  if(!root)
  {
      DebugPrintf(" - failed to open directory\n");
      return;
  }
  if(!root.isDirectory())
  {
      DebugPrintf(" - not a directory\n");
      return;
  }

  File file = root.openNextFile();
  while(file)
  {
      if(file.isDirectory())
      {
          DebugPrintf("DIR : %s\n", file.name());
          if(levels)
          {
              // Recursive call to list subdirectories
              ListDir(fs, file.path(), levels -1);
          }
      } 
      else 
      {
        lastWrite = file.getLastWrite();
        if((line++ % 2) == 1)
          DebugPrintf("%28s %10d %10.3f\n"  , file.name(), file.size(), 
              (float)(timeNow - lastWrite) / (float)(60*60*24));
        else
          DebugPrintf("%28s %10d %10.3f    ", file.name(), file.size(), 
              (float)(timeNow - lastWrite) / (float)(60*60*24));
      }
      file = root.openNextFile();
  }
  uint32_t used  = LITTLEFS.usedBytes();
  uint32_t total = LITTLEFS.totalBytes();
  DebugPrintf("\nTotalBytes %d, UsedBytes %d = %d%% full.\n", total, used, 100 * ((float)used / (float)total)); 
}

void onTelnetInput(String str) 
{
  TelnetActive = 1;
  CommandInput(str);
}
 
void CommandInput(String str) 
{
char Msg[21];
int i;
char *name;
char *ptr = (char *)str.c_str();

  DebugPrintf("Got Command %s\n", ptr);
  // checks for a certain command
  if(strncmp(ptr, "user", 4) == 0) 
  {
    ptr += 5;
    DebugPrintf("Got user %s\n", ptr);
    if(!strcmp(ptr, &Username[0]))
    {
      DebugPrintf("Telnet: user OK\n");
      LogedIn |= 1;
      return;
    }
  } 
  else if(strncmp(ptr, "pass", 4) == 0) 
  {
    ptr += 5;
    DebugPrintf("Got pass %s\n", ptr);
    if(!strcmp(ptr, &Password[0]))
    {
      DebugPrintf("Telnet: pass OK\n");
      LogEvent(LITTLEFS, "User %s loged in.", Username);
      LogedIn |= 2;
      return;
    }
  }
  else if (str == "ping") 
  {
    telnet.println("> pong"); 
    DebugPrintf("Telnet: pong\n");
  } 
  else if (str == "bye") 
  {
    telnet.print("> disconnecting you...\n");
    telnet.disconnectClient();
    LogedIn = 0;
  } 
  else if(LogedIn == 3)
  {
    if(strncmp(ptr, "ftpuser", 7) == 0) 
    {
      DebugPrintf("Got ftpuser\n");
      ptr += 8;
      strcpy(&ftp_user[0], ptr);  
    }
    else if(strncmp(ptr, "ftppass", 7) == 0) 
    {
      DebugPrintf("Got ftppass\n");
      ptr += 8;
      strcpy(&ftp_pass[0], ptr); 
    }
    else if(strncmp(ptr, "ftpserver", 9) == 0) 
    {
      DebugPrintf("Got ftpserver\n");
      ptr += 10;
      strcpy(&ftp_server[0], ptr);
    }
    else if(strncmp(ptr, "upgrade", 7) == 0) 
    {
      DebugPrintf("Got upgrade\n");
      LogEvent(LITTLEFS, "Ota via FTP Upgrade", EMPTY);
      LoadFirmwareFromFTP();
    }
    else if(str == "sendlog") 
    {
      DebugPrintf("Force FTP the log file\n");
      ForcedLogFile = 1;
    }
    else if(strncmp(ptr, "sendfile", 8) == 0) 
    {
      DebugPrintf("Force FTP a file\n");
      SendSingleFile(LITTLEFS, ptr + 9);
    }
    else if(str == "sendsum") 
    {
      DebugPrintf("Force FTP the Summery file\n");
      SendSummaryFile(LITTLEFS);
    }
    else if(strncmp(ptr, "showreg", 7) == 0) 
    {
      ptr += 8;
      ShowMBregs = atoi(ptr);
      DebugPrintf("Got Reg %s(%d) remember to zero it!\n", ptr, ShowMBregs);
    }
    else if(str == "clrips") 
    {
      DebugPrintf("Clear All IP's\n");
      ClearAllIPs();
    }
    else if(strncmp(ptr, "clean", 5) == 0) 
    {
      
      CleanDir(LITTLEFS, "/", 1);
      for(i = 0; i < MAX_NAME_LIST; i++)
      {
        if(FileNameList[i][0] == '\0')
          break;
        if(deleteFile(LITTLEFS, FileNameList[i]))
        {
          DebugPrintf("    - File   [%s] deleted successfully\n", FileNameList[i]);
        } 
        else 
        {
          DebugPrintf("    - Delete [%s] failed\n", FileNameList[i]);
        }
        FileNameList[i][0] = '\0';
      }
    }
    else if (str == "dir") 
    {
      ListDir(LITTLEFS, "/", 2);
    }
    else if(strncmp(ptr, "writefile", 3) == 0) 
    {
      ptr += 10;
      name = ptr;           // save filename
      while(*ptr != ' ')
        ptr++;
      *ptr = '\0';          // terminate filename
      ptr++;                // point to data to write
      if(writeFile(LITTLEFS, name, ptr) > 0) 
      {
        DebugPrintf("Wrote file: %s[%s]\n", name, ptr);
      }
      else
      {
        DebugPrintf("Failed to write %s[%s]\n", name, ptr);
      }
    }
    else if(strncmp(ptr, "cat", 3) == 0) 
    {
      ptr += 4;
      DebugPrintf("cat file: [%s]\n", ptr);
      if(LITTLEFS.exists(ptr))
      {
        DoCatCmnd(LITTLEFS, ptr);
      }
    }
    //else if (str == "del") 
    else if(strncmp(ptr, "del", 3) == 0) 
    {
      ptr += 4;
      DebugPrintf("del file: [%s]\n", ptr);
      if(LITTLEFS.exists(ptr))
      {
        DebugPrintf("Deleting file: %s\n", ptr);
    
        if(LITTLEFS.remove(ptr))
        {
          DebugPrintf("File deleted successfully\n");
        } 
        else 
        {
          DebugPrintf("Delete failed\n");
        }
      } 
      else 
      {
        DebugPrintf("File [%s] does not exist\n", ptr);
      }
    }
    else if (str == "logs") 
    {
      ShowLogEvent(DeadDummy);
    }
    else if (str == "info") 
    {
      telnet.printf("%s %04d/%02d/%02d %02dH%02d Uptime %d:%d:%d \n", VERSION, 
        Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min, 
        (Uptime / 60) / 24, (Uptime / 60) % 24, Uptime % 60);
    } 
    else if(!strncmp(ptr, "msg", 3)) 
    {
      sprintf(Msg, "NextMessPos = %d", NextMessPos);
      NoInitMsg(Msg);
    } 
    else if(!strncmp(ptr, "listmsg", 7)) 
    {
      ListNoInitMess();
    } 
    else if(!strncmp(ptr, "rmmsg", 5)) 
    {
      RmNoInitMess();
    } 
    else if(!strncmp(ptr, "debug?", 6)) 
    {
      telnet.printf("Debug is %d(DataNoInitCount = %d RtcNoInitCount = %d...\n", Debug, DataNoInitCount, RtcNoInitCount);
      DataNoInitCount++;
      RtcNoInitCount++;
    } 
    else if(!strncmp(ptr, "debug", 5)) 
    {
      ptr += 6;
      Debug = atoi(ptr);
      telnet.printf("Set Debug to %d...\n", Debug);
      if(Debug == 0)
      {
        DataNoInitCount = 0;
        RtcNoInitCount = 0;
      }
    } 
    else if(strstr(str.c_str(), "reboot")) 
    {
      telnet.print("> Reset CPU ...\n");
      telnet.flush();
      DebugPrintf("Reboot command\n"); 
      sleep(10);
      telnet.disconnectClient();
      DoResetRequierd("Telnet reboot"); 
    }
    else
    {
      telnet.printf("Unkown command [%s] use logs/info/clrips/dir/del/writefile/cat/clean/sendsum/sendlog/sendfile/reboot/ftpuser/ftppass/ftpserver/upgrade/msg/listmsg/rmmsg/debug=X/showreg/bye\n", str);
    }
  }
  else
  {
    telnet.printf("Unkown command [%s] use ping/user/pass/bye\n", str);
  }
}

int GetNetworkTime(void)
{  
int ret = 0;

  // Init and get the time
  configTime(GmtOffset_sec, DaylightOffset_sec, NtpServer);
  //Get time
  if(getLocalTime(&Now))
  {
    rtc.setTimeStruct(Now); 
    if(Debug > 2) 
      DebugPrintf("CURRENT TIME WROTE TO RTC is: %04d/%02d/%02d %02dH%02d\n",
        Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min);
    return(1);
  }
  if(Debug > 2) 
    DebugPrintf("Current time obtained from RTC after NTP config is: %04d/%02d/%02d %02dH%02d\n",
      Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min);
  return(ret);

}

int ReadLogEvent(fs::FS &Fs)
{
char CurrentEventStr[6];
int ret;

  DebugPrintf("Open /eventlog.txt for reading\n");
  File file = Fs.open("/eventlog.txt");
  if(!file || file.isDirectory())
  {
    DebugPrintf("Failed to open /eventlog.txt for reading\n");
    return(-1);
  }
  if((ret = file.read((unsigned char *)EventLog, sizeof(EventLog))) > 0)
  {
    DebugPrintf("File /eventlog.txt read %d bytes\n", ret);
  } 
  else 
  {
    DebugPrintf("Read logfile failed\n");
    file.close();
    return(-1);
  }
  file.close();
  if(readFile(LITTLEFS, "/CurrentEvent.txt", &CurrentEventStr[0]) > 0) 
  {
    CurrentEvent = atoi(&CurrentEventStr[0]);
  }
  DebugPrintf("Overwrite %s\n", EventLog[CurrentEvent]);
  return(ret);
}

void ShowLogEvent(WiFiClient Client)
{
int event, idx;

  idx = CurrentEvent - 1;
  if(idx < 0)
  {
    idx = MAX_EVENT - 1;
  }
  for(event = 0; event < MAX_EVENT; event++)
  { 
    if(!isdigit(EventLog[idx][0]))
    {
      idx--;
      if(idx < 0)
      {
        idx = MAX_EVENT - 1;
      }
      continue;
    }
    if(Client != DeadDummy)
    {
      Client.printf("%s", EventLog[idx]);
      Client.println("<br>");
    }
    else
    {
      DebugPrintf("%s\n", EventLog[idx]);
    }
    idx--;
    if(idx < 0)
    {
      idx = MAX_EVENT -1;
    }
  }
}

int LogEvent(fs::FS &Fs, const char *Format, const char *Param)
{
char CurrentEventStr[6];
int ret;

  sprintf(EventLog[CurrentEvent], "%04d/%02d/%02d %02dH%02d   ",
    Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min);
  sprintf(EventLog[CurrentEvent] + 18, Format, Param);

  DebugPrintf("Writing log : [%s]\n", EventLog[CurrentEvent]);
  CurrentEvent++;
  if(CurrentEvent >= MAX_EVENT)
  {
    CurrentEvent = 0;
  }
  File file = Fs.open("/eventlog.txt", FILE_WRITE);
  if(file)
  {
    if((ret = file.write((unsigned char *)EventLog, sizeof(EventLog))) > 0)
    {
      DebugPrintf("File /eventlog.txt written %d bytes\n", ret);
     
      file.close();
      sprintf(&CurrentEventStr[0], "%d", CurrentEvent);
      if(writeFile(LITTLEFS, "/CurrentEvent.txt", &CurrentEventStr[0]) > 0) 
      {
        return(ret);
      }
      else
      {
        DebugPrintf("Failed to write /CurrentEvent.txt\n");
      }
    }
    else 
    {
      DebugPrintf("Write failed\n");
    }
  }
  else
  {
    DebugPrintf("Failed to open /eventlog.txt for writing\n");
  }
  LITTLEFS.end();
  if(!LITTLEFS.begin(FORMAT_LITTLEFS_IF_FAILED))
  {
    DebugPrintf("LITTLEFS ReMount Failed\n");
  }
  DebugPrintf("LITTLEFS ReMount OK\n");
  return(-1);
}

void DisplayHTMLpage(WiFiClient Client)
{
  // Display the HTML web page
  Client.println("<!DOCTYPE html><html>");
  Client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  Client.println("<title>MBmonitor</title>");
  Client.println("<link rel=\"icon\" href=\"data:,\">");
  // CSS to style the on/off buttons 
  // Feel free to change the background-color and font-size attributes to fit your preferences
  Client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
  Client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 14px 35px;");
  Client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
  Client.println(".button2 {background-color: #555555;}</style></head>");
 
  // Web Page Heading
  String Str = String(SiteName);
  Client.println("<body><h1>" + Str + " Modbus Monitor</h1>");
  Client.println(WiFi.localIP());
}
  
int ShowUserPass(WiFiClient Client, char *HeadLine, int Info)
{
String line;
char *ptr;
float temp;
int i;

  // String = %2FTimeOn=12H00&%2FTimeOff=16H00&%2FSwitchOn=11111&%2FSwitchOff=+0

  line = Client.readStringUntil('\r');
  DebugPrintf("Got %d : [%s]\n", line.length(), line);
    ptr = &(((char *)line.c_str())[0]);
    for(int i = 0; i < line.length(); i++)
    {
      DebugPrintf("%c", *(ptr + i));
    }
    DebugPrintf("\n");

  if(ptr = strstr( line.c_str(), "user"))
  {
    ptr += 5;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(Username, ptr);
    *End = '&';
  }
  DebugPrintf("User = %s\n", Username);
  if(ptr = strstr( line.c_str(), "password"))
  {
    ptr += 9;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(Password, ptr);
    *End = '&';
  }
  DebugPrintf("Password = %s\n", Password);
  // Web Page Heading
  DisplayHTMLpage(Client);
  Client.println("<FORM action=\"/\" method=\"post\">");

  // Display current state
  if(Info)
  {
    Client.printf("<p>WiFi RSSI db: %d Time: %02dH%02d %s (%d:%d:%d)</p>", 
          RSSIval, Now.tm_hour, Now.tm_min, VERSION, 
          (Uptime / 60) / 24, (Uptime / 60) % 24, Uptime % 60);

  
  }
  else
  {
    uint8_t baseMac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    if (ret == ESP_OK) 
    {
      DebugPrintf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
      Client.printf("<p>STAtion MAC Address : %02x:%02x:%02x:%02x:%02x:%02x</p>\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
    } 
    else 
    {
      DebugPrintf("Failed to read STA MAC address\n");
    }
    ret = esp_wifi_get_mac(WIFI_IF_AP, baseMac);
    if (ret == ESP_OK) 
    {
      DebugPrintf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
      Client.printf("<p>APoint MAC Address : %02x:%02x:%02x:%02x:%02x:%02x</p>\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
    } 
    else 
    {
      DebugPrintf("Failed to read MAC AP address\n");
    }
  }

  Client.println(HeadLine);

  Client.print("Username  ");
  Client.print("<input type=\"text\" name=\"/user\" id=\"/user\" value=\"");
  Client.print(Username);
  Client.println("\">");
  Client.println("<br>");

  Client.print("Password  ");
  Client.print("<input type=\"text\" name=\"/password\" id=\"/password\" value=\"");
  Client.print(Password);
  Client.println("\">");
  Client.println("<br>");
           
  Client.println("<input type=\"submit\" value=\"Submit\">");
  Client.println("</form>");
  Client.println("<br>");
  Client.println("<br>");

  Client.println("<p><a href=\"/ct/refrech\"><button class=\"button button2\">OK</button></a></p>");

  if(!strcmp(Username, "QWERTY"))
  {
    if(!strcmp(Password, "QWERTY"))
    {
      DebugPrintf("Backdoor reset [%s] [%s] \n", Username, Password);
      if(writeFile(LITTLEFS, "/username.txt", " ")) // Write the complete file
      {
        if(writeFile(LITTLEFS, "/password.txt", " ")) // Write the complete file
        {
          DoResetRequierd("Usr/Pass forced"); 
          //sprintf(&RTCmessage[0], "Usr/Pass forced");
        }
      }
      DebugPrintf("Backdoor reset failed\n");
    }
  }
  if((strlen(Username) > 4) && (strlen(Password) > 4))
    return(1);
  else
    return(-1);
}

void ShowCreateUser(WiFiClient Client)
{

  Username[0] = '\0';
  Password[0] = '\0';
  if(ShowUserPass(Client, (char *)"<p>Now you must create a user with password</p>", 0) > 0)
  {
    if(writeFile(LITTLEFS, "/username.txt", &Username[0])) // Write the complete file
    {
      if(writeFile(LITTLEFS, "/password.txt", &Password[0])) // Write the complete file
      {
        LogEvent(LITTLEFS, "Usr/Pass created", EMPTY);
        UserFound = 1;
        return;
      }
    }
    DebugPrintf("Write user/pass failed\n");
    return;
  }
  DebugPrintf("Get create user/pass failed\n");
}

void SaveCredentials(void)
{
char tmp[11];

  if(writeFile(LITTLEFS, "/WiFiSSID.txt", &WiFiSSID[0])) 
  {
    if(writeFile(LITTLEFS, "/WiFiPassword.txt", &WiFiPassword[0])) 
    {
      DebugPrintf("Local_IP bytes %3d.%3d.%3d.%3d\n", Local_IP[0], Local_IP[1], Local_IP[2], Local_IP[3]);
      if(writeBinFile(LITTLEFS, "/Local_IP.txt", (unsigned char *)&Local_IP, sizeof(IPAddress))) 
      {
        DebugPrintf("IP bytes %3d.%3d.%3d.%3d\n", Gateway[0], Gateway[1], Gateway[2], Gateway[3]);
        if(writeBinFile(LITTLEFS, "/Gateway.txt", (unsigned char *)&Gateway, sizeof(IPAddress))) 
        {
          if(writeBinFile(LITTLEFS, "/Subnet.txt", (unsigned char *)&Subnet, sizeof(IPAddress))) 
          {
            PrimaryDNS = Gateway;
            if(writeBinFile(LITTLEFS, "/PrimaryDNS.txt", (unsigned char *)&PrimaryDNS, sizeof(IPAddress))) 
            {
              SecondaryDNS = Gateway;
              if(writeBinFile(LITTLEFS, "/SecondaryDNS.txt", (unsigned char *)&SecondaryDNS, sizeof(IPAddress))) 
              {
                if(writeFile(LITTLEFS, "/SiteName.txt", &SiteName[0])) 
                {
                  if(writeFile(LITTLEFS, "/NtpServer.txt", &NtpServer[0])) 
                  {
                    sprintf(tmp, "%d", GmtOffset_sec);
                    if(writeFile(LITTLEFS, "/GmtOffset_sec.txt", &tmp[0])) 
                    {
                      sprintf(tmp, "%d", DaylightOffset_sec);     // xxxxxxxxxxx
                      if(writeFile(LITTLEFS, "/DaylightOffset_sec.txt", &tmp[0])) 
                      {

                        DebugPrintf("Credentials written\n");
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
} 

IPAddress MakeIPAddress(char *Text)
{
char *ptr, *dot;

  if((dot = strchr(Text, '.')) != NULL)
  {
    *dot = '\0';
    NewIP[0] = atoi(Text);
    *dot = '.';
    Text = dot + 1;
    if((dot = strchr(Text, '.')) != NULL)
    {
      *dot = '\0';
      NewIP[1] = atoi(Text);
      *dot = '.';
      Text = dot + 1;
      if((dot = strchr(Text, '.')) != NULL)
      {
        *dot = '\0';
        NewIP[2] = atoi(Text);
        *dot = '.';
        Text = dot + 1;
        
        NewIP[3] = atoi(Text);
        //DebugPrintf("NewIP bytes %3d.%3d.%3d.%3d\n", NewIP[0], NewIP[1], NewIP[2], NewIP[3]);
        return(NewIP);
      }
    }
  }
  return(IPDummy);
}

int ShowCredentials(WiFiClient Client, char *HeadLine)
{
String line;
char *ptr;
float temp;

  // String = %2FTimeOn=12H00&%2FTimeOff=16H00&%2FSwitchOn=11111&%2FSwitchOff=+0

  line = Client.readStringUntil('\r');
  DebugPrintf("Got %d : [%s]\n", line.length(), line);
    ptr = &(((char *)line.c_str())[0]);
    for(int i = 0; i < line.length(); i++)
    {
      DebugPrintf("%c", *(ptr + i));
    }
    DebugPrintf("\n");

  if(ptr = strstr( line.c_str(), "WiFiSSID"))
  {
    ptr += 9;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(WiFiSSID, ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "WiFiPassword"))
  {
    ptr += 13;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(WiFiPassword, ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "Local_IP"))
  {
    ptr += 9;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    Local_IP = MakeIPAddress(ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "Gateway"))
  {
    ptr += 8;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    Gateway = MakeIPAddress(ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "Subnet"))
  {
    ptr += 7;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    Subnet = MakeIPAddress(ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "GmtOffset"))
  {
    ptr += 10;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    GmtOffset_sec = atoi(ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "DaylightOffset"))
  {
    ptr += 15;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    DaylightOffset_sec = atoi(ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "NtpServer"))
  {
    ptr += 10;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(NtpServer, ptr);
    *End = '&';
  }
  if(ptr = strstr( line.c_str(), "SiteName"))
  {
    ptr += 9;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(SiteName, ptr);
    *End = '&';
  }
  // Web Page Heading
  DisplayHTMLpage(Client);
  // Display current state
  Client.println(HeadLine);

  Client.println("<FORM action=\"/\" method=\"post\">");

    uint8_t baseMac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    if (ret == ESP_OK) 
    {
      DebugPrintf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
      Client.printf("<p>STAtion MAC Address : %02x:%02x:%02x:%02x:%02x:%02x</p>\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
    } 
    else 
    {
      DebugPrintf("Failed to read STA MAC address\n");
    }
    ret = esp_wifi_get_mac(WIFI_IF_AP, baseMac);
    if (ret == ESP_OK) 
    {
      DebugPrintf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
      Client.printf("<p>APoint MAC Address : %02x:%02x:%02x:%02x:%02x:%02x</p>\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
    } 
    else 
    {
      DebugPrintf("Failed to read MAC AP address\n");
    }
  Client.print("WiFi SSID");
  Client.print("<input type=\"text\" name=\"/WiFiSSID\" id=\"/WiFiSSID\" value=\"\">");
  Client.println("<br>");
  Client.print("WiFi Password");
  Client.print("<input type=\"text\" name=\"/WiFiPassword\" id=\"/WiFiPassword\" value=\"\">");
  Client.println("<br>");
  Client.print("Local IP");
  Client.print("<input type=\"text\" name=\"/Local_IP\" id=\"/Local_IP\" value=\"\">");
  Client.println("<br>");
  Client.print("Gateway IP");
  Client.print("<input type=\"text\" name=\"/Gateway\" id=\"/Gateway\" value=\"\">");
  Client.println("<br>");
  Client.print("Subnet mask");
  Client.print("<input type=\"text\" name=\"/Subnet\" id=\"/Subnet\" value=\"\">");
  Client.println("<br>");
  Client.print("NTP Server (pool.ntp.org)");
  Client.print("<input type=\"text\" name=\"/NtpServer\" id=\"/NtpServer\" value=\"\">");
  Client.println("<br>");
  Client.print("GMT offset(in sec eg. 7200)");
  Client.print("<input type=\"text\" name=\"/GmtOffset\" id=\"/GmtOffset\" value=\"\">");
  Client.println("<br>");
  Client.print("Daylight Saving(in sec eg. 360");
  Client.print("<input type=\"text\" name=\"/DaylightOffset\" id=\"/DaylightOffset\" value=\"\">");
  Client.println("<br>");
  Client.print("Site Name");
  Client.print("<input type=\"text\" name=\"/SiteName\" id=\"/SiteName\" value=\"\">");
  Client.println("<br>");

  Client.println("<input type=\"submit\" value=\"Submit\">");
  Client.println("</form>");
  Client.println("<br>");
  Client.println("<br>");

  Client.println("<p><a href=\"/ct/refrech\"><button class=\"button button2\">Reboot</button></a></p>");

  if((strlen(WiFiPassword) > 4) && (strlen(WiFiSSID) > 4))
    return(1);
  else
    return(-1);
}

void ShowLogin(WiFiClient Client)
{
char temp[22];

  Username[0] = '\0';
  Password[0] = '\0';
  if(ShowUserPass(Client, (char *)"<p>You must log in with user and password</p>", 1) > 0)
  {
    memset(temp, 0, sizeof(temp));
    if(readFile(LITTLEFS, "/username.txt", &temp[0]) > 0) // Read the complete file
    {
      DebugPrintf("Compare User [%s] [%s] \n", temp, Username);
      if(!strcmp(Username, temp))
      {
        memset(temp, 0, sizeof(temp));
        if(readFile(LITTLEFS, "/password.txt", &temp[0]) > 0) // Read the complete file
        {
          DebugPrintf("Compare Password [%s] [%s] \n", temp, Password);
          if(!strcmp(Password, temp))
          {
            UserFound = 1;
            UserLogedIn = 1;
            LogedInIP = Client.remoteIP();
            LogedInIPtimeout = timeNow + 1200; // 20 min timeout
            LogEvent(LITTLEFS, "User %s loged in", Username);
            return;
          }
        }
      }
    }
  }
  DebugPrintf("Get login user/pass failed\n");
}

void ShowSettings(WiFiClient Client)
{ 
int i, allWritten = 0;
String line;
char *ptr, txtStr[15];
float temp;

  line = Client.readStringUntil('\r');
  DebugPrintf("Got %d : %s\n", line.length(), line);

  ptr = &(((char *)line.c_str())[0]);
  for(i = 0; i < line.length(); i++)
  {
    DebugPrintf("%c", *(ptr + i));
  }
  DebugPrintf("\n");

  if(ptr = strstr( line.c_str(), "LogTimeDay"))
  {
    ptr += 11;
    DebugPrintf("Log Time Send = ");
    
    if((isdigit(*(ptr + 0))) && (isdigit(*(ptr + 1))) && (*(ptr + 2) == 'H'))
    {
      *(ptr + 2) = '\0';
      LogTimeHour = atoi(ptr);
      *(ptr + 2) = 'H';
      ptr += 3;
    
      if((isdigit(*(ptr + 0))) && (isdigit(*(ptr + 1))))
      {
        LogTimeMin = atoi(ptr);
        DebugPrintf("%02dH%02d\n", LogTimeHour, LogTimeMin);
        sprintf(LogOnTime, "%02dH%02d\n", LogTimeHour, LogTimeMin);
      }
    }
  } 
  if(ptr = strstr( line.c_str(), "LogInterval"))
  {
    ptr += 12;
    DebugPrintf("Log Interval = ");
    
    if((isdigit(*(ptr + 0))) && (isdigit(*(ptr + 1))))
    {
      *(ptr + 2) = '\0';
      LogIntSec = atoi(ptr);
      *(ptr + 2) = ' ';
    
      DebugPrintf("%02d\n", LogIntSec);
      sprintf(LogIntervalSec, "%02d\n", LogIntSec);
    }
  } 
  if(ptr = strstr( line.c_str(), "SiteName"))
  {
    ptr += 9;
    char *End = ptr;
    while(*End != '&')
      End++;
    *End = '\0';
    strcpy(SiteName, ptr);
    *End = '&';
    DebugPrintf("SiteName = %s\n", SiteName);
  } 
  // &%2FSndLg=Send+Log
  if(ptr = strstr( line.c_str(), "SndLg"))
  {
    ptr += 6;
    DebugPrintf("Got SendLg\n");
    if(*ptr == '1')
    {
      DebugPrintf("Do Send File NOW\n\n"); 
      SendSingleFile(LITTLEFS, NewFileName);
    } 
  }
  DisplayHTMLpage(Client);
  
  Client.printf("<p>WiFi RSSI db: %d Time: %02dH%02d %s (%d:%d:%d)</p>", 
          RSSIval, Now.tm_hour, Now.tm_min, VERSION, 
          (Uptime / 60) / 24, (Uptime / 60) % 24, Uptime % 60);

  Client.printf("<p>Serial No. : %c%c%c%c%c%c%c%c%c%c Flash %dMB, SPIRam %dKB</p>",
             SerialNo[0], SerialNo[1], SerialNo[2], SerialNo[3], SerialNo[4],
             SerialNo[5], SerialNo[6], SerialNo[7], SerialNo[8], SerialNo[9],
             ESP.getFlashChipSize() / (1024 * 1024),
             ESP.getPsramSize() / 1024); 

  Client.printf("<p>PV1 : %8.2f V %8.2f A %8.2f W</p>", 
      (float)Regs[REG_6D_PV1_V].Value * Regs[REG_6D_PV1_V].Factor,
      (float)Regs[REG_6E_PV1_A].Value * Regs[REG_6E_PV1_A].Factor,
      (float)Regs[REG_BA_PV1_W].Value * Regs[REG_BA_PV1_W].Factor);
  Client.printf("<p>PV2 : %8.2f V %8.2f A %8.2f W</p>", 
      (float)Regs[REG_6F_PV2_V].Value * Regs[REG_6F_PV2_V].Factor,
      (float)Regs[REG_70_PV2_A].Value * Regs[REG_70_PV2_A].Factor,
      (float)Regs[REG_BB_PV2_W].Value * Regs[REG_BB_PV2_W].Factor);

  i = REG_7E_BAT_BMS_V;
  while(Regs[i].Name)
  {
    Client.printf("<p>%s : %8.2f</p>", Regs[i].Name, (float)Regs[i].Value * Regs[i].Factor);
    i++;
  }
  Client.printf("<p>BAT Charging  AH %8.2f</p>", ChgTotalAH);
  Client.printf("<p>BAT Discharge AH %8.2f</p>", DisTotalAH);
  Client.printf("<p>PV  Energy    WH %8.2f</p>", PVtotalWH);

  Client.println("<p><a href=\"/ct/refrech\"><button type=\"text\" name=\"/Refr\" id=\"/refr\" value=\"\"class=\"button type=\"text\" name=\"/Refr\" id=\"/refr\" value=\"\"button3\">Refresh</button></a></p>");

  Client.println("<FORM action=\"/\" method=\"post\">");

  sprintf(txtStr, "%s", LogOnTime);
  Client.println("Time to send LogFile(eg. 19H00)");
  Client.print("<input type=\"text\" name=\"/LogTimeDay\" id=\"/LogTimeDay\" value=\"");
  Client.print(txtStr);
  Client.println("\">");
  Client.println("<br>");

  sprintf(txtStr, "%s", LogIntervalSec);
  Client.println("Log Interval between samples in seconds(eg. 10)");
  Client.print("<input type=\"text\" name=\"/LogInterval\" id=\"/LogInterval\" value=\"");
  Client.print(txtStr);
  Client.println("\">");
  Client.println("<br>");

  sprintf(txtStr, "%s", SiteName);
  Client.println("Site name(name for the inverter)");
  Client.print("<input type=\"text\" name=\"/SiteName\" id=\"/SiteName\" value=\"");
  Client.print(txtStr);
  Client.println("\">");
  Client.println("<br>");

  Client.println("Send LogFile(Set to 1 to send now");
  Client.print("<input type=\"text\" name=\"/SndLg\" id=\"/SndLg\" value=\"");
  Client.print("0");
  Client.println("\">");
  Client.println("<br>");

  Client.println("FTP server(IP assress)");
  Client.print("<input type=\"text\" name=\"/ftpserver\" id=\"/ftpserver\" value=\"");
  Client.printf("%s", ftp_server);
  Client.println("\">");
  Client.println("<br>");

  Client.println("FTP user(username)");
  Client.print("<input type=\"text\" name=\"/ftpuser\" id=\"/ftpuser\" value=\"");
  Client.printf("%s", ftp_user);
  Client.println("\">");
  Client.println("<br>");

  Client.println("FTP pass(password)");
  Client.print("<input type=\"text\" name=\"/ftppass\" id=\"/ftppass\" value=\"");
  Client.printf("%s", ftp_pass);
  Client.println("\">");
  Client.println("<br>");

  Client.println("<input type=\"submit\" value=\"Apply all settings\">");
  Client.println("</form>");
  Client.println("<br>");

  ShowLogEvent(Client);

  if(line.length())
  {
    if(writeFile(LITTLEFS, "/LogInterval.txt", &LogIntervalSec[0])) 
    {
        if(writeFile(LITTLEFS, "/LogOnTime.txt", &LogOnTime[0])) 
        {
          if(writeFile(LITTLEFS, "/SiteName.txt", &SiteName[0])) 
          {
            if(writeFile(LITTLEFS, "/ftpserver.txt", &ftp_server[0]))
            {
              if(writeFile(LITTLEFS, "/ftpuser.txt", &ftp_user[0]))
              {
                if(writeFile(LITTLEFS, "/ftppass.txt", &ftp_pass[0]))
                {
                  DebugPrintf("Settings written.\n");
                  LogEvent(LITTLEFS, "Settings modified", EMPTY);
                  allWritten = 1;
                }
              }
            }
          }
        }
    }
    if(allWritten == 0)
      DebugPrintf("Settings write FAILED\n");
  }
  Client.println("<br>");
}

void ClearAllIPs(void)
{
  if(deleteFile(LITTLEFS, "/WiFiSSID.txt")) 
  {
    if(deleteFile(LITTLEFS, "/WiFiPassword.txt")) 
    {
      if(deleteFile(LITTLEFS, "/Local_IP.txt")) 
      {
        if(deleteFile(LITTLEFS, "/Gateway.txt")) 
        {
          if(deleteFile(LITTLEFS, "/Subnet.txt")) 
          {
            if(deleteFile(LITTLEFS, "/PrimaryDNS.txt")) 
            {
              if(deleteFile(LITTLEFS, "/SecondaryDNS.txt")) 
              {
                if(deleteFile(LITTLEFS, "/SiteName.txt")) 
                {
                  if(deleteFile(LITTLEFS, "/username.txt")) 
                  {
                    if(deleteFile(LITTLEFS, "/password.txt")) 
                    {
                      DebugPrintf("Credentials deleted\n");
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void setup() 
{

  char txtStr[15];

  Empty[0] = '\0';
  Serial.begin(115200);

  // Note the format for setting a serial port is as follows: 
  //    HwSerial2.begin(baud-rate, protocol, RX pin, TX pin);
  //     1 stop bit if parity is used-2 bits if no parity
  HwSerial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

// free size in the flash memory
  Serial.printf("\n\nFlash sise %d MB, PsRAM %d KB\n\n",  ESP.getFlashChipSize() / (1024 * 1024),
                                                      ESP.getPsramSize() / 1024); 

  HwSerial2.setRxBufferSize(1024);
  HwSerial2.onReceiveError(OnReceiveError); 

  DebugPrintf("Serial Txd is on pin: %d\n", TXD2);
  DebugPrintf("Serial Rxd is on pin: %d\n", RXD2);
  // Modbus 
  node.begin(1, HwSerial2);
  // Callbacks allow us to configure the RS232 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  if(!LITTLEFS.begin(FORMAT_LITTLEFS_IF_FAILED))
  {
      DebugPrintf("LITTLEFS Mount Failed\n");
      return;
  }
  DebugPrintf("LITTLEFS Mount OK\n");

  LogIntSec = 10;
  sprintf(LogIntervalSec, "%02d\n", LogIntSec);
  CredentialsNeeded = 1;
  timeNow = time(&timeNow);
  if(readBinFile(LITTLEFS, "/Local_IP.txt", (unsigned char *)&Local_IP, sizeof(IPAddress)) > 0) 
  {
    DebugPrintf("Local_IP bytes %3d.%3d.%3d.%3d\n", Local_IP[0], Local_IP[1], Local_IP[2], Local_IP[3]);
    if(!((Local_IP[0] == 10) || (Local_IP[0] == 192)))
    {
      ClearAllIPs();
      DoResetRequierd("Local IP corrupt"); 
    }
    if(readBinFile(LITTLEFS, "/Gateway.txt", (unsigned char *)&Gateway, sizeof(IPAddress)) > 0 ) 
    {
      DebugPrintf("Gateway IP bytes %3d.%3d.%3d.%3d\n", Gateway[0], Gateway[1], Gateway[2], Gateway[3]);
      if(readBinFile(LITTLEFS, "/Subnet.txt", (unsigned char *)&Subnet, sizeof(IPAddress)) > 0) 
      {
        DebugPrintf("Subnet %3d.%3d.%3d.%3d\n", Subnet[0], Subnet[1], Subnet[2], Subnet[3]);
        if(readBinFile(LITTLEFS, "/PrimaryDNS.txt", (unsigned char *)&PrimaryDNS, sizeof(IPAddress)) > 0)  
        {
          DebugPrintf("DNS1 %3d.%3d.%3d.%3d\n", PrimaryDNS[0], PrimaryDNS[1], PrimaryDNS[2], PrimaryDNS[3]);
          if(readBinFile(LITTLEFS, "/SecondaryDNS.txt", (unsigned char *)&SecondaryDNS, sizeof(IPAddress)) > 0) 
          {
            DebugPrintf("DNS2 %3d.%3d.%3d.%3d\n", SecondaryDNS[0], SecondaryDNS[1], SecondaryDNS[2], SecondaryDNS[3]);
            if(readFile(LITTLEFS, "/WiFiSSID.txt", &WiFiSSID[0]) > 0) 
            {
              if(readFile(LITTLEFS, "/WiFiPassword.txt", &WiFiPassword[0]) > 0)  
              {
                if(readFile(LITTLEFS, "/SiteName.txt", &SiteName[0]) > 0) 
                {
                  if(readFile(LITTLEFS, "/NtpServer.txt", &NtpServer[0]) > 0) 
                  {
                    memset(txtStr, 0, sizeof(txtStr));
                    if(readFile(LITTLEFS, "/GmtOffset_sec.txt", &txtStr[0]) > 0) 
                    {
                      sscanf(txtStr, "%ld", &GmtOffset_sec);
                      DebugPrintf("GMT Ofset %d\n", GmtOffset_sec);
                      memset(txtStr, 0, sizeof(txtStr));
                      if(readFile(LITTLEFS, "/DaylightOffset_sec.txt", &txtStr[0]) > 0) 
                      {
                        sscanf(txtStr, "%d", &DaylightOffset_sec);
                        DebugPrintf("DayLight %d\n", DaylightOffset_sec);
                        if(readFile(LITTLEFS, "/LogInterval.txt", &LogIntervalSec[0]) > 0) 
                        {
                          sscanf(LogIntervalSec, "%2d", &LogIntSec);
                          DebugPrintf("LogInterval = %02d\n", LogIntSec);
                        }
                        CredentialsNeeded = 0;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if(CredentialsNeeded == 1)
  {
    //WiFi.mode(WIFI_STA);
    DebugPrintf("Connect to WiFi as AP\n");
    WiFi.mode(WIFI_AP);
    WiFi.setHostname(HOSTNAME);
    WiFi.softAPConfig(IPAddress(Ap_ip), IPAddress(Ap_gateway), IPAddress(Ap_subnet));
    //WiFi.softAP(Ap_ssid, Ap_password);
    WiFi.softAP("MBmonitor", NULL);    // The temporary SSID for device
    IPAddress IP = WiFi.softAPIP();

    ////set hostname
    //mdns_hostname_set("my-esp32");
    ////set default instance
    //mdns_instance_name_set("Jhon's ESP32 Thing");

    DebugPrintf("\nIP = %d.%d.%d.%d\n", IP[0], IP[1], IP[2], IP[3]);
  }
  else
  {
    // Initialize Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setHostname(SiteName);
    // Configures stored IP address
    if(!WiFi.config(Local_IP, Gateway, Subnet, PrimaryDNS, SecondaryDNS)) {
      DebugPrintf("STA Failed to configure\n");
    }
    WiFi.setAutoReconnect(true);
    // Connect to Wi-Fi network with SSID and WiFiPassword




// xxxxxxxxxxxxxxxxxxxxx
//strcpy(WiFiSSID, "AJWLAN");
// xxxxxxxxxxxxxxxxxxxxx





    DebugPrintf("Connecting to %s\n", WiFiSSID);
    WiFi.begin(WiFiSSID, WiFiPassword);
    while (WiFi.status() != WL_CONNECTED) 
    {
      Serial.print('.');
      delay(1000);
    }
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80); 

    DebugPrintf("\nIP = %d.%d.%d.%d\n",
        WiFi.localIP()[0],
        WiFi.localIP()[1],
        WiFi.localIP()[2],
        WiFi.localIP()[3]);
    if(readFile(LITTLEFS, "/LogOnTime.txt", &LogOnTime[0]) > 0) 
    {
      LogTimeHour = atoi(&LogOnTime[0]);
      LogTimeMin  = atoi(&LogOnTime[3]);
    }
  }  
  Serial.print("RSSI = "); 
  Serial.println(WiFi.RSSI());  // Print WiFi signal strength
  RSSIval = WiFi.RSSI();

  pinMode(BlueLEDpin , OUTPUT);
  digitalWrite(BlueLEDpin, LOW);

  setupTelnet();
  HttpServer.begin();
  if(!GetNetworkTime())
  {
    Now = rtc.getTimeStruct();
    if(Debug > 2) 
      DebugPrintf("Current time from RTC is: %04d/%02d/%02d %02dH%02d\n",
        Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min);
  }
  timeNow = time(&timeNow);
  
  UserLogedIn = 0;
  UserFound = 0;
  memset(Username, 0, sizeof(Username));
  if(readFile(LITTLEFS, "/username.txt", &Username[0]) >= 5) // Read the complete file
  {
    DebugPrintf("User = %s\n", Username);
    memset(Password, 0, sizeof(Password));
    if(readFile(LITTLEFS, "/password.txt", &Password[0]) >= 5) // Read the complete file
    {
      DebugPrintf("Password = %s\n", Password);
      if((strlen(Username) > 4) && (strlen(Password) > 4))
        UserFound = 1;
    }
  }

  ReadLogEvent(LITTLEFS);
  if(readFile(LITTLEFS, "/SiteName.txt", &SiteName[0]) > 0) 
  {
    DebugPrintf("%s/n", SiteName);
    if(readFile(LITTLEFS, "/ftpserver.txt", &ftp_server[0]))
    {
      Serial.println(ftp_server);
      if(readFile(LITTLEFS, "/ftpuser.txt", &ftp_user[0]))
      {
        Serial.println(ftp_user);
        if(readFile(LITTLEFS, "/ftppass.txt", &ftp_pass[0]))
        {
          Serial.println(ftp_pass);
          DebugPrintf("ftp_server [%s], ftp_user [%s], ftp_pass[%s]\n",
                  ftp_server,ftp_user,ftp_pass);
        }
      }
    }
  }
  readFile(LITTLEFS, "/BootMessage.txt", &BootMessage[0]); 
  Rebooted = 1;
  DebugPrintf("Configuring WDT...\n");
  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);               //add current thread to WDT watch

  NextMessPos = 0;
  while(1)
  {
    if((NoInitMess[NextMessPos].len > 0) && (NoInitMess[NextMessPos].len <= MAX_MSG) &&
       (NoInitMess[NextMessPos].len == strlenMAX_MSG(NoInitMess[NextMessPos].mesg)) &&
       (NoInitMess[NextMessPos].sum == AddUp(NoInitMess[NextMessPos].len, NoInitMess[NextMessPos].mesg))) 
    {
      DebugPrintf("%2d Message len = %d [%s]\n", NextMessPos,
                   NoInitMess[NextMessPos].len, NoInitMess[NextMessPos].mesg);
      NextMessPos++;
    }
    else
      break;
  }
  memset(FileNameList, 0, sizeof(FileNameList));
  SumFileSeq = 0;
  DebugPrintf("Stored Messages : %d\n", NextMessPos);
}


int strlenMAX_MSG(char *Ptr)
{
int len = 0;

  if(Ptr[len] == '\0')
    return(0);
  while((len <= MAX_MSG) && (Ptr[len] != '\0'))
  {
    len++;
    if(Ptr[len] == '\0')
      return(len);
  }
  return(-1);
}

int AddUp(int len, char* mesg)
{
int i, sum = 0;

  for(i = 0; i < len; i++)
    sum += mesg[i];
  return(sum);
}

void NoInitMsg(const char *Msg)
{
  DebugPrintf("Add message at %d\n", NextMessPos);
  sprintf(NoInitMess[NextMessPos].mesg, "%2d %04d/%02d/%02d %02dH%02d %s", NextMessPos,
    Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min, Msg);
  NoInitMess[NextMessPos].len = strlenMAX_MSG(NoInitMess[NextMessPos].mesg);
  NoInitMess[NextMessPos].sum =         AddUp(NoInitMess[NextMessPos].len, 
                                              NoInitMess[NextMessPos].mesg);
  if(++NextMessPos >= MAX_MSG)
     NextMessPos = 0;
}

void RmNoInitMess(void)
{
int i;

  for(i = 0; i < MAX_MSG; i++)
     NoInitMess[i].len = 0;
}

void ListNoInitMess(void)
{
int i;

  DebugPrintf("List %d messages:\n", NextMessPos);
  for(i = 0; i < NextMessPos; i++)
      DebugPrintf("%d %s\n", i, NoInitMess[i].mesg);
}

                          
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
    if(Debug > 2)
      DebugPrintf("\nSerial ERRORS %d %d %d %d %d %d\n", 
          Errors[0],
          Errors[1],
          Errors[2],
          Errors[3],
          Errors[4],
          Errors[5]);
  }
  return(0);
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
        if(Debug > 4) DebugPrintf("Got 6D\n");
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
      if((Start + 500) <= millis())
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
        if(Debug > 4) DebugPrintf("Got A0\n");
        Regs[REG_AD_POW1_W   ].Value = node.getResponseBuffer(Regs[REG_AD_POW1_W   ].Reg_no - BLOCK_A0_START);
        Regs[REG_B7_BAT_CH_V ].Value = node.getResponseBuffer(Regs[REG_B7_BAT_CH_V ].Reg_no - BLOCK_A0_START);
        Regs[REG_B8_BAT_PERS ].Value = node.getResponseBuffer(Regs[REG_B8_BAT_PERS ].Reg_no - BLOCK_A0_START);
        Regs[REG_BF_BAT_DIS_C].Value = node.getResponseBuffer(Regs[REG_BF_BAT_DIS_C].Reg_no - BLOCK_A0_START);
        Regs[REG_BA_PV1_W    ].Value = node.getResponseBuffer(Regs[REG_BA_PV1_W    ].Reg_no - BLOCK_A0_START);
        Regs[REG_BB_PV2_W    ].Value = node.getResponseBuffer(Regs[REG_BB_PV2_W    ].Reg_no - BLOCK_A0_START);
        Regs[REG_A0_GRID_A   ].Value = node.getResponseBuffer(Regs[REG_A0_GRID_A   ].Reg_no - BLOCK_A0_START);
        State = STATE_IDLE_2;
        if(Debug > 4)
          DebugPrintf("\nLoop %d for %dms\n", Loop++, millis() - Start);
      }
      else if(!((result == node.ku8MBPollWaitRx) || (result == node.ku8MBPollWaitRest))) 
      {
        State = STATE_IDLE_2;       // There was some other error
      }
      return(result);

    case STATE_IDLE_2:
      if((Start + 500) <= millis())
      {
        Start = millis();
        if(ShowMBregs == 0)
          State = STATE_REQUEST_6D;
        else
          State = STATE_REQ_SHOW;
      }
      return(node.ku8MBPollWaitRx);

    case STATE_REQ_SHOW:
      node.readPollHoldingRegisters(ShowMBregs, 2);
      State = STATE_WAIT_SHOW;
      return(node.ku8MBPollWaitRx);

    case STATE_WAIT_SHOW:  
      result = node.PollModbus();
      if(result == node.ku8MBSuccess)
      {
        if(Debug > 2) DebugPrintf("Got %04X\n", ShowMBregs);
        ShowRegValue = node.getResponseBuffer(0);
        DebugPrintf("Requested Register %04X = %d\n", ShowMBregs, ShowRegValue);
        State = STATE_IDLE_3;
      }
      else if(!((result == node.ku8MBPollWaitRx) || (result == node.ku8MBPollWaitRest))) 
      {
        State = STATE_IDLE_3;       // There was some other error
      }

    case STATE_IDLE_3:
      if((Start + 500) <= millis())
      {
        Start = millis();
        State = STATE_REQUEST_6D;
      }
      return(node.ku8MBPollWaitRx);
  }
  return(-1);
}

int TransferLogFile(fs::FS &Fs)
{
int len = 0;

  ReadFile = Fs.open(StoreFileName, FILE_READ);
  if(!ReadFile)
  {
    DebugPrintf("Log - failed to open file for reading\n");
    return(0);
  }
  DebugPrintf("FTP to %s as %s with %s\n", ftp_server, ftp_user, ftp_pass);
  ftp.OpenConnection();
  ftp.InitFile("Type A");
  ftp.NewFile(&StoreFileName[1]);
  FtpSendDone = 0;
  ftp.WriteClientUpCall();
  ftp.CloseFile();
  ftp.CloseConnection();
  ReadFile.close(); 
  if(FtpSendDone == 0)
  {
    DebugPrintf("Failed to ftp file %s\n", StoreFileName);
  }
  else
  {
    if(deleteFile(LITTLEFS, StoreFileName)) 
      DebugPrintf("Delteted file %s\n", StoreFileName);
    else
      DebugPrintf("Failed to delete file %s\n", StoreFileName);
  }
  DebugPrintf("FTP file done\n");
  return(FtpSendDone);
}

void StartLogToFile(fs::FS &fs)
{
  sprintf(&NewFileName[0], "/MBmonLog%04d-%02d-%02d_%02dH%02d.csv",
        Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min);
  DebugPrintf("Writing Log file: %s\n", NewFileName);

  LogFile = fs.open(NewFileName, FILE_WRITE);
  if(!LogFile)
  {
    DebugPrintf("Log - failed to open file for writing\n");
    sleep(1);
    return;
  }
  LogFile.println("Seq;Power W;BatBMS V;BatCHG V;GRID V;GRID A;BatPers %;BatCur A;CHG tot AH;DIS tot AH;Pv AH");
  LogFileSize = 75;
  LoggingOn = 1;
  LogStartTime = time(&LogStartTime);
}

void StopLogToFile(void)
{
  LoggingOn = 0;
  LogFile.close();
  DebugPrintf("File closed, size = %d\n", LogFileSize);
  LogFileSize = 0;
}

void DoLogToFile(void)
{
char buffer[180];
time_t Now;

  sprintf(&buffer[0], "%d;%3.0f;%3.2f;%3.2f;%3.2f;%3.2f;  %3.0f;%3.2f;  %3.2f;%3.2f;%3.0f\n",
        time(&Now) - LogStartTime,

        Regs[REG_AD_POW1_W   ].Value * Regs[REG_AD_POW1_W   ].Factor,
        Regs[REG_7E_BAT_BMS_V].Value * Regs[REG_7E_BAT_BMS_V].Factor,
        Regs[REG_B7_BAT_CH_V ].Value * Regs[REG_B7_BAT_CH_V ].Factor,
        Regs[REG_8C_GRID_V   ].Value * Regs[REG_8C_GRID_V   ].Factor, 
        Regs[REG_A0_GRID_A   ].Value * Regs[REG_A0_GRID_A   ].Factor, 

        Regs[REG_B8_BAT_PERS ].Value * Regs[REG_B8_BAT_PERS ].Factor,
        Regs[REG_BF_BAT_DIS_C].Value * Regs[REG_BF_BAT_DIS_C].Factor,

        ChgTotalAH,
        DisTotalAH,
        PVtotalWH);
    
  LogFileSize += strlen(buffer);
  if(LogFile.print(buffer))
  {
    if(Debug > 2) DebugPrintf("File %s written, size = %d\n", NewFileName, LogFileSize);
  } 
  else 
  {
    DebugPrintf("Log write failed\n");
  }
}

void LogToSummaryFile(fs::FS &Fs)
{

  if(SummeryFileName[0] == 'x')
  {
    sprintf(&SummeryFileName[0], "/MBmonSummery.csv");
  }
  sprintf(SummeryBuffer, "%d; %04d-%02d-%02d_%02dH%02d; %8.2f; %8.2f; %8.2f\n", SumFileSeq++,
        Now.tm_year + 1900, Now.tm_mon + 1, Now.tm_mday, Now.tm_hour, Now.tm_min,
                 ChgTotalAH, DisTotalAH, PVtotalWH);

  DebugPrintf("Writing Sum file: %s\n", SummeryFileName);

  SummFile = Fs.open(SummeryFileName, FILE_APPEND);
  if(!SummFile)
  {
    DebugPrintf("Summery - failed to open file for writing\n");
    return;
  }
  if(SummFile.print(SummeryBuffer))
  {
    DebugPrintf("Summery file written\n");
  } 
  else 
  {
    DebugPrintf("Summery write failed\n");
  }
  SummFile.close(); 
  DebugPrintf("Log to Summery file done\n");
}

void SendSingleFile(fs::FS &Fs, char *File)
{

  DebugPrintf("FTP file %s(Log = %s)\n", File, NewFileName);

  // check for MBmonLog in filenames like "MBmonLog2026-05-19_16H25.csv"
  if((strncmp(File, NewFileName, 24) == 0) || (File == NewFileName))
  {
    if(LoggingOn)
    {
      LogFile.close();
      DebugPrintf("File %s closed, size = %d\n", NewFileName, LogFileSize);
    }
    else
      DebugPrintf("Not Loging\n");
  }
  else
    DebugPrintf("Not LogFile\n");

  SummReadFile = Fs.open(File, FILE_READ);
  if(!SummReadFile)
  {
    DebugPrintf("Send a file - failed to open file for reading\n");
    return;
  }
  ftp.OpenConnection();
  ftp.InitFile("Type A");
  ftp.NewFile(&File[1]);
  FtpSendDone = 0;

  FtpSendingSummery = 1;
  ftp.WriteClientUpCall();
  FtpSendingSummery = 0;

  if(FtpSendDone == 0)
  {
    DebugPrintf("Failed to ftp file %s\n", File);
  }
  ftp.CloseFile();
  ftp.CloseConnection();
  if((strncmp(File, NewFileName, 24) == 0) || (File == NewFileName))
  {
    if(LoggingOn)
    {
      LogFile.close();
      LogFile = Fs.open(NewFileName, FILE_APPEND);
      DebugPrintf("File %s reopened, size = %d\n", NewFileName, LogFileSize);
    }
  }
  DebugPrintf("FTP file done\n");
  sprintf(&SummeryFileName[0], "/MBmonSummery.csv");
  SummReadFile.close();
}

void SendSummaryFile(fs::FS &Fs)
{
  SummReadFile = Fs.open(SummeryFileName, FILE_READ);
  if(!SummReadFile)
  {
    DebugPrintf("Summery - failed to open file for reading\n");
    return;
  }
  ftp.OpenConnection();
  ftp.InitFile("Type A");
  ftp.NewFile(&SummeryFileName[1]);
  FtpSendDone = 0;

  FtpSendingSummery = 1;
  ftp.WriteClientUpCall();
  FtpSendingSummery = 0;

  if(FtpSendDone == 0)
  {
    DebugPrintf("Failed to ftp file %s\n", SummeryFileName);
  }
  ftp.CloseFile();
  ftp.CloseConnection();
  DebugPrintf("FTP Summery file done\n");
  sprintf(&SummeryFileName[0], "/MBmonSummery.csv");
  SummReadFile.close();
  SumFileSeq = 0;
}

void CheckSerialConsoleInput(void)
{
int GotLine = 0;
static int CharCnt = 0;
static char InLine[77] = "";

  if (Serial.available() > 0) 
  {
    InLine[CharCnt] = Serial.read();
    if((InLine[CharCnt] == '\n') || (InLine[CharCnt] == '\r'))
    {
      InLine[CharCnt] = '\0';
      GotLine = 1;
    }  
    else
    {
      CharCnt++;
    }
  }
  else
    return;
  if(GotLine)
  {
    Serial.printf("You entered: %s\n", InLine);
    CommandInput(String(InLine));
    CharCnt = 0;
  }
}

void loop()
{
int i, k, result; 
static int Last = millis();
static int LogLast = millis();
time_t Tnow;
static int FirstTime = 1;
static int Cnt = 0;
static float ChgAH = 0;
static float DisAH = 0;
static float PV_W  = 0;
static int SumDoneDay = 33;
static int StoreMin = 63;
static int ledOn = 0;
static int ledOnTime = 0;
static int ledOffTime = 0;
static int ledTimer = 0;
static int lastPing = -1; 
static int lastPingOK = -1; 
static time_t networkTime = 0;
static time_t nextMin = 0;
static int lastWD = millis();
static int lastPrt = 0;
WiFiClient httpClient = HttpServer.available();   // Listen for incoming Http clients

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
    
        SerialNo[0] = node.getResponseBuffer(3) >> 8;
        SerialNo[1] = node.getResponseBuffer(3) & 0xff;
        SerialNo[2] = node.getResponseBuffer(4) >> 8;
        SerialNo[3] = node.getResponseBuffer(4) & 0xff;
        SerialNo[4] = node.getResponseBuffer(5) >> 8;
        SerialNo[5] = node.getResponseBuffer(5) & 0xff;
        SerialNo[6] = node.getResponseBuffer(6) >> 8;
        SerialNo[7] = node.getResponseBuffer(6) & 0xff;
        SerialNo[8] = node.getResponseBuffer(7) >> 8;
        SerialNo[9] = node.getResponseBuffer(7) & 0xff;
        DebugPrintf("Serial No. : %c%c%c%c%c%c%c%c%c%c\n",
             SerialNo[0], SerialNo[1], SerialNo[2], SerialNo[3], SerialNo[4],
             SerialNo[5], SerialNo[6], SerialNo[7], SerialNo[8], SerialNo[9]);
        break;
      }
      else
      {
        if(Debug > 5) DebugPrintf("Fail %d at SerialNo. Ret = %02X\n", i, result);
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
    if(Debug > 4)
    {
      i = 0;
      while(Regs[i].Name)
      {
        DebugPrintf("%s 0X%04X: %8.2f\n", Regs[i].Name, Regs[i].Reg_no, (float)Regs[i].Value * Regs[i].Factor);
        i++;
      }
      DebugPrintf("BAT Charging  AH %8.2f\n", ChgTotalAH);
      DebugPrintf("BAT Discharge AH %8.2f\n", DisTotalAH);
      DebugPrintf("PV Energy     WH %8.2f\n", PVtotalWH);
      i = 0;
      while(i < 2000)       // Wait 2 seconds after prints
      {
        ModbusPollLoop();
        delay(2);
        i += 2;
      }
    }
  }
  else
  {
    if(!((result == node.ku8MBPollWaitRx) || (result == node.ku8MBPollWaitRest))) 
       if(Debug > 2)DebugPrintf("Fail %02X reading %04X len %d\n", result, BLOCK_A0_START, BLOCK_A0_LENGTH);
  }

  if((Last + 2000) <=  millis())        // Every 2 seconds
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
      DisTotalAH += DisAH / (float)(30 * 60);  // Avarage over 1 min and scale to hour     
      ChgTotalAH += ChgAH / (float)(30 * 60);  // Avarage over 1 min and scale to hour     
      PVtotalWH  += PV_W  / (float)(30 * 60);  // Avarage over 1 min and scale to hour     
      DisAH = 0;
      ChgAH = 0;
      PV_W = 0;
    }
  }



  // resetting WDT every 5s
  if (millis() - lastWD >= 5000) 
  {
    if(Debug > 5) DebugPrintf("Resetting WDT...\n");
    esp_task_wdt_reset();
    lastWD = millis();
  }
 
  timeNow = time(&timeNow);
  if((timeNow % 3600) <= 10) // every 1 hour
  {
    if(networkTime <= timeNow)
    {
      GetNetworkTime();
      timeNow = time(&timeNow);
      networkTime = timeNow + 100;
    }
  }
  if(nextMin == 0)     // start counting one hour elapsed periods
  {
    nextMin = timeNow + 60; 
    Uptime = 0;
  }
  if(timeNow >= nextMin)     // one min elapsed
  {
    nextMin = timeNow + 60; 
    Uptime++;
    if(Debug > 5) DebugPrintf("Uptime %d\n", Uptime);
  }
  if(LogedInIPtimeout)
  {
    if(LogedInIPtimeout <= timeNow)
    {
      DebugPrintf("User LOGIN expired at %d.\n", timeNow - LogedInIPtimeout);
      LogedInIPtimeout = 0;
      LogedInIP = (IPAddress)(0, 0, 0, 0);
      UserLogedIn = 0;
    }
  }
  getLocalTime(&Now);

  telnet.loop();

  if(httpClient) 
  {                             // If a new HttpClient connects,
    CurrentTime = millis();
    PreviousTime = CurrentTime;
    DebugPrintf("New Http Client.\n");          // print a message out in the serial port
    String currentLine = "";                    // make a String to hold incoming data from the HttpClient
    while (httpClient.connected() && CurrentTime - PreviousTime <= TimeoutTime) 
    {  // loop while the HttpClient's connected
      CurrentTime = millis();
      if (httpClient.available()) 
      {             // if there's bytes to read from the HttpClient,
        char c = httpClient.read();             // read a byte, then
        DebugPrintf("%c", c);                   // print it out the serial monitor
        Header += c;
        if (c == '\n') 
        {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the HttpClient HTTP request, so send a response:
          if (currentLine.length() == 0)
          {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the HttpClient knows what's coming, then a blank line:
            httpClient.println("HTTP/1.1 200 OK");
            httpClient.println("Content-type:text/html");
            httpClient.println("Connection: close");
            httpClient.println();
            
            if(CredentialsNeeded)
            {
              if(ShowCredentials(httpClient, (char *)"<p>You must enter the network credentials</p>") > 0)
              {
                httpClient.println("After reboot close this AP WiFi, connect to main WiFi and open configured Modbus Monitor IP in browser");
                DebugPrintf("Got credentials for [%s]\n", SiteName);
                SaveCredentials();
                DoResetRequierd("Credentials changed"); 
              }
            }
            else if(UserLogedIn)
            {
              if(LogedInIP == httpClient.remoteIP())
              {
                ShowSettings(httpClient);
                LogedInIPtimeout = timeNow + 1200; // 20 min timeout
    
                //ShowLogEvent(httpClient);
              }
              else
              {
                DebugPrintf("User LOGIN cleared.\n");
                LogedInIPtimeout = 0;
                LogedInIP = (IPAddress)(0, 0, 0, 0);
                UserLogedIn = 0;
                ShowLogin(httpClient);
              }
            }
            else
            {
              if(UserFound)
              {
                ShowLogin(httpClient);
              }
              else
              {
                ShowCreateUser(httpClient);
              }
            }
            
            httpClient.println("</body></html>");
            
            // The HTTP response ends with another blank line
            httpClient.println();
            // Break out of the while loop
            break;
          } 
          else 
          { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } 
        else if (c != '\r') 
        {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    Header = "";
    currentLine = ""; 
    // Close the connection
    httpClient.stop();
    DebugPrintf("Client disconnected.\n");
    DebugPrintf("\n");
  }

  if(((millis() - lastPing) > 5000) || (lastPing == -1)) 
  {
    bool success = Ping.ping(Gateway);
    lastPing = millis();
    if(!success)
    {
      DebugPrintf("Ping failed\n");
    }
    else
    {
      if(Debug > 5) DebugPrintf("Ping succesful.(%d)\n", Now.tm_sec);
      lastPingOK = millis();
    }
    if(((millis() - lastPingOK) > 60000) && (lastPingOK != -1)) 
    {
      DebugPrintf("Ping failed for 60 seconds.\n");

// xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
//      DoResetRequierd("Ping failed "); 
// xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

    }
  } 
  if(ResetRequierd)
  { 
    if(ResetRequierd <= timeNow)
    {
      DebugPrintf("Reset requested...\n");
      esp_restart();
    }
    if(timeNow != lastPrt)
    {
      DebugPrintf("Reset requested at %d\n", ResetRequierd - timeNow);
      lastPrt = timeNow;
    }
  }
  if(ledTimer <= millis())
  {
    ledOnTime = START_LED_ON;
    ledOffTime = START_LED_OFF;
    if(ledOn)
    { 
      ledOn = 0;
      ledTimer = millis() + ledOffTime;
      if(Debug > 4) DebugPrintf("Led off %d.\n", ledOffTime);
      digitalWrite(BlueLEDpin,   LOW);
    }
    else
    {
      ledOn = 1;
      ledTimer = millis() + ledOnTime;
      if(Debug > 4) DebugPrintf("Led ON %d.\n", ledOnTime);
      digitalWrite(BlueLEDpin,   HIGH);
    }
  }
  if(LoggingOn)
  {
    if((LogLast + (LogIntSec * 1000)) <=  millis())        // Every LogIntSec seconds
    {
      if(LogFile)
      {
        DoLogToFile();
        LogLast = millis();
      }
    }
  }
  else
  {
    StartLogToFile(LITTLEFS);
    if(Debug > 2) DebugPrintf("Start new logging to (%s)\n", NewFileName);
    LogFileDay  = Now.tm_mday;
    LogToSummaryFile(LITTLEFS);
    ChgTotalAH  = 0;
    DisTotalAH  = 0;
    PVtotalWH   = 0;
    DoLogToFile();
  }
  if(StoreFileName[0])
  {
    if((FtpFailTime == 0) || (time(&Tnow) < (FtpFailTime + 120)))   // fail is older than 2 min
    {
      if(Debug > 2) DebugPrintf("Upload (%s)\n", StoreFileName);
      if(LITTLEFS.exists(StoreFileName))
      {
        if(TransferLogFile(LITTLEFS))
        {
          if(Debug > 2) DebugPrintf("Upload done (%s)\n", StoreFileName);
          LITTLEFS.remove(StoreFileName);
          StoreFileName[0] = '\0';
          ForcedLogFile = 0;
          FtpFailTime = 0;
        }
        else
        {
          FtpFailTime = time(&Tnow);
          if(Debug > 2) DebugPrintf("Upload FAILED (%s)\n", StoreFileName);
        }
      } 
    } 
  }

  // From the manual Telnet "sendlog" command
  if(ForcedLogFile) 
  {
    if(LogFile)
    {
      if(Debug > 2) DebugPrintf("STOP LOGGING TO (%s)\n", NewFileName);
      strcpy(&StoreFileName[0], &NewFileName[0]);
      StopLogToFile();
      StartLogToFile(LITTLEFS);
      LogFileDay  = Now.tm_mday;
      LogToSummaryFile(LITTLEFS);
      ChgTotalAH  = 0;
      DisTotalAH  = 0;
      PVtotalWH   = 0;
    }
  }
  //When time got to the "Send Log Time" setting
  if(LogFileDay  != Now.tm_mday) 
  {
    if((Now.tm_hour == LogTimeHour) && (Now.tm_min == LogTimeMin) && (Now.tm_min != StoreMin))
    {
      if(LoggingOn)
      {
        if(LogFile)
        {
          if(Debug > 2) DebugPrintf("Stop logging to (%s)\n", NewFileName);
          strcpy(&StoreFileName[0], &NewFileName[0]);
          StopLogToFile();
          StartLogToFile(LITTLEFS);
          LogFileDay  = Now.tm_mday;
          LogToSummaryFile(LITTLEFS);
          ChgTotalAH  = 0;
          DisTotalAH  = 0;
          PVtotalWH   = 0;
        }
      }
    }
  }
  StoreMin = Now.tm_min;
  if(Rebooted)
  {
    LogEvent(LITTLEFS, "Reboot(%s)", &BootMessage[0]);
    NoInitMsg(&BootMessage[0]);
    Rebooted = 0;
    BootMessage[0] = '\0';
    writeFile(LITTLEFS, "/BootMessage.txt", " "); 
    LogFileDay--;
  }
  CheckSerialConsoleInput();
}

