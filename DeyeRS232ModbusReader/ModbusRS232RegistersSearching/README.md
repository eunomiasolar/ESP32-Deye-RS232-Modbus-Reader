<div align = center>

ESP32 Deye RS232 Modbus register searcher

</div>

# Description

To read data from my Deye Inverter (Moddle SUN-5K-SG03LP1-EU)

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

