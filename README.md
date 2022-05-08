# RanchRangerTx
---
The transmitter portion of the target shooting game Ranch Ranger.  See
[RanchRangerRx](TODO) for a complete description of the game.


### Peripherals Exercised
---------------------
* `Board_DIO1` - Dipswitch #1.  Inidicates a 1 point target.
* `Board_DIO12` - Dipswitch #2.  Inidicates a 5 point target.
* `Board_DIO15` - Dipswitch #3.  Inidicates a 10 point target.
* `Board_DIO16_TDO` - Dipswitch #4.  Inidicates a 25 point target.
* `Board_DIO17_TDI` - Dipswitch #5.  Inidicates a 100 point target.
* `Board_DIO21` - Dipswitch #6.  Unused.
* `Board_DIO7` - Digital target input.  0 normally, 1 when target is hit.

### Board Specific Settings
-----------------------
1. The default frequency is:
    - 433.92 MHz for the CC1350-LAUNCHXL-433
    - 433.92/490 MHz for the CC1352P-4-LAUNCHXL
    - 2440 MHz on the CC2640R2-LAUNCHXL
    - *868.0 MHz for other launchpads*
In order to change frequency, modify the smartrf_settings.c file.

### Application Design Details
--------------------------
RanchRangerTx begins in Standby mode.  The Sensor Controller domain then
waits for the target switch to trigger before waking up the System CPU.
Once awake, the System CPU sends a single radio packet to be picked up
by RanchRangerRx.  We use the EasyLink API to access the RF driver, set the
frequency and transmit packets.

A single task, "ranchRangerTxFn", configures the RF driver through the EasyLink
API and transmits messages.

### Frame Structure
-----------------
The EasyLink implements a basic header for transmitting and receiving data. This header supports
addressing for a star or point-to-point network with acknowledgments.

Packet structure:

     _________________________________________________________
    |           |                   |                         |
    | 1B Length | 1-64b Dst Address |         Payload         |
    |___________|___________________|_________________________|

Payload structure:

     ________________________________________________________
    |               |                   |                    |
    | 2B Sequence # | 64b Unique ID     | 1B Dipswitch State |
    |_______________|___________________|____________________|

The sequence number is not currently used for anything, but may in the future.
The 64 bit unique identifier is unique to the chip and is used by
RanchRangerRx to prevent double counting of targets.  The dipswitch state
is 8 bits, although only the lowest 6 are currently set according the
dipswitch on the board.  The highest 2 bits should always be 0.
