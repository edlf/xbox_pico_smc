# PIC pinout/assignment

| Pin | Function  | Group   | Xbox           | In/out | Notes                   | Pico assignment |
|-----|-----------|---------|----------------|--------|-------------------------|-----------------|
|  1  | !MCLR     |         | SMC Reset      | Input  | Brown out triggers this | RUN             |
|  2  | RA0       | PORT A  | Red Led        | Output | buffered                | GPIO2           |
|  3  | RA1       | PORT A  | Green Led      | Output | buffered                | GPIO3           |
|  4  | RA2       | PORT A  | Sys reset      | Output | has 10k pull up         | GPIO4           |
|  5  | RA3       | PORT A  | Eject switch   | Input  | has 10k pull up         | GPIO5           |
|  6  | RA4/T0CKI | PORT A  | SMI*           | Output | has 10k pull up         | GPIO6           |
|  7  | RA5       | PORT A  | DVD Eject      | Output | has 1k pull up          | GPIO7           |
|  8  | Vss       |         | Ground         |        |                         | GND             |
|  9  | CLKIN     |         | Clock in       | Input  |                         | Not connected   |
| 10  | CLKOUT    |         | Clock out      |        |                         | Not connected   |
| 11  | RC0/T1CKI | PORT C  | Audio Clamp    | Output | Buffered                | GPIO8           |
| 12  | RC1       | PORT C  | Power OK       | Input  |                         | GPIO9           |
| 13  | RC2       | PORT C  | Fan PWM1       | Output |                         | GPIO10          |
| 14  | RC3/SCL   | PORT C  | SMBus SCL      | In/Out |                         | GPIO13          |
| 15  | RC4/SDA   | PORT C  | SMBus SDA      | In/Out |                         | GPIO12          |
| 16  | RC5       | PORT C  | Power ON       | Output |                         | GPIO11          |
| 17  | RC6       | PORT C  | RTC Dump       | Output |                         | GPIO14          |
| 18  | RC7       | PORT C  | Power switch   | Input  |                         | GPIO15          |
| 19  | Vss       |         | Ground         |        |                         | GND             |
| 20  | Vdd       |         | 3.3 Standby    |        |                         | 3.3V            |
| 21  | RB0/Int   | PORT B  | PLL Enable     | Output |                         | GPIO16          |
| 22  | RB1       | PORT B  | Video mode0    | Input  | Standby 10k pull up     | GPIO17          |
| 23  | RB2       | PORT B  | Video mode1    | Input  | POW_ON 10k pull up      | GPIO18          |
| 24  | RB3       | PORT B  | Video mode2    | Input  | Standby 10k pull up     | GPIO19          |
| 25  | RB4       | PORT B  | Tray state 0   | Input  |                         | GPIO20          |
| 26  | RB6       | PORT B  | Tray state 1   | Input  |                         | GPIO21          |
| 27  | RB7       | PORT B  | Tray state 2   | Input  |                         | GPIO22          |
| 28  | RB7       | PORT B  | DVD Active     | Input  |                         | GPIO26          |

Xbox seems to run the PIC at 10Mhz, so 400ns instruction cycle.

Pico GPIO0 and GPIO1 are reserved for UART
