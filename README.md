# Exchange-Fan-Data-Log

This program controls operation of the air exchange fan, and logs data 
from four temperature sensors as well as the switches on the sliding
deck door and windows in the bedroom and office.  An additional input
to controlling the fan which is also logged is the furnace control.

The harware uses the Adafruit MCP9808 temperature sensors (4), the
Adafruit DS1307 real time clock, and relay module.

Pins SCL & SDA are used for addressing the temp sensors as well as the Real Time Clock
Pins 8, 11, 12, and 13 are used for communicating between the Arduino
and Andee.

The computer is an Arduino Uno with an Annikken Andee board to write 
to a micro SD card and communicate with an IOS product.

Data is logged to the SD card in csv format which can be read by Excel.
Logging is done at intervals set by the constant interval (in seconds).
The format of the data is:
-  Date number (number of days since 1/2/1904, the base used by Mac Excel)
-  Time of day (number of seconds since midnight, which must be converted 
    in Excel to a fraction of the day by dividing by 86400)
-  Temperature from sensor 1 (Outside air)
-  Temperature from sensor 2 (Outside air after exchange)
-  Temperature from sensor 3 (Inside air)
-  Temperature from sensor 4 (Inside air after exchange)
-  % of time movingAvg
-      desired %
-      actual %
-      adjustment enabled
-      run mode state
-  Status of Bedroom windows (0 = open, 1 = closed)
-  Status of Office windows
-  Status of Sliding Deck Door
-  Status of Furnace signal
-  Status of Air Exchange Fan (1 = running, 0 = not running)

The exchange fan is set to running if requested by the furnace and
all windows and the door are closed, except that the door signal is 
delayed to avoid turning the fan on and off excessively when the door
is used for a quick entry or exit from the house.  The delay is set by
constant doorDelay (in seconds).

The time running percent may be adjusted by the computer, and there are
buttons for setting the requested percent, reporting the current
percent, toggling enablement of adjustment.  The percentage calculated
time running per on/off cycle.
