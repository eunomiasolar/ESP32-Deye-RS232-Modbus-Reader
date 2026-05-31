<div align = center>

ESP32 Basic Deye RS232 Modbus reader

</div>

# Description

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
  6) GRID Volt and Amps
    
    Example output
        RegisterName  Address  Value
        PV1 V         0X006D:   199.40
        PV1 A         0X006E:     8.50
        PV1 W         0X00BA:  1664.00 
        PV2 V         0X006F:   196.70
        PV2 A         0X0070:     8.60
        PV2 W         0X00BB:  1657.00
        BAT BMS V     0X007E:    54.71   
        BAT CH  V     0X00B7:    54.41
        POW1    W     0X00AD:   514.00
        GRID    V     0X008C:    5.00
        GRID    A     0X00A0:    0.00
        BAT %         0X00B8:    67.00
        DISCHG  A     0X00BF:   -48.58 

