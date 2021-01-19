*Дополнительная информация* 
====================

### Плата BVP_61850  ###

- BVP_61850_v2.4

    | №   | Сигнал     | XF1     | XF2     | XF3     | IMR_61850_v1.1  |
    |:----|:-----------|:--------|:--------|:--------|:----------------|
    | A1  | 5V         | 5V      | 5V      | 5V      | -               |
    | A2  | 2V5        | 2V5     | 2V5     | 2V5     | 2V5             |
    | A3  | GND        | GND     | GND     | GND     | GND             |
    | A4  | GND        | GND     | GND     | GND     | GND             |
    | A5  | USB_.D+    | DN1.D+  | DN2.D+  | DN3.D+  | USB_UP.D+       |
    | A6  | USB_.D-    | DN1.D-  | DN2.-   | DN3.D-  | USB_UP.D-       |
    | A7  | GND        | GND     | GND     | GND     | GND             |
    | A8  | -          | -       | -       | -       | -               |
    | A9  | -          | -       | -       | -       | -               |
    | A10 | GND        | GND     | GND     | GND     | GND             |
    | A11 | SCL        | SCL0    | SCL0    | SCL0    | I2C_SCL         |
    | A12 | SDA        | SDA0    | SDA0    | SDA0    | I2C_SDA         |
    | A13 | GND        | GND     | GND     | GND     | GND             |
    | A14 | RASP_GPIO  | GPIO22  | GPIO16  | GPIO4   | MDIO1           |
    | A15 | RASP_GPIO  | GPIO23  | GPIO17  | GPIO5   | MDC1            |
    | A16 | RASP_GPIO  | GPIO24  | GPIO18  | GPIO6   | nINT            |
    | A17 | RASP_GPIO  | GPIO25  | GPIO19  | GPIO7   | -               |
    | A18 | RASP_GPIO  | GPIO26  | GPIO20  | GPIO12  | PGP5            |
    | A19 | RASP_GPIO  | GPIO27  | GPIO21  | GPIO13  | PGP6            |
    | A20 | RASP_GPIO  | GPIO43  | GPIO44  | GPIO45  | RESET_N         |

    | №   | Сигнал     | XF1     | XF2     | XF3     | IMR_61850_v1.1  |
    |:----|:-----------|:--------|:--------|:--------|:----------------|
    | B1  | 3V3        | 3V3     | 3V3     | 3V3     | 3V3             |
    | B2  | 3V3        | 3V3     | 3V3     | 3V3     | 3V3             |
    | B3  | 1V2        | 1V2     | 1V2     | 1V2     | 1V2             |
    | B4  | 1V2        | 1V2     | 1V2     | 1V2     | 1V2             |
    | B5  | -          | -       | -       | -       | -               |
    | B6  | -          | -       | -       | -       | -               |
    | B7  | GND        | GND     | GND     | GND     | GND             |
    | B8  | -          | -       | -       | -       | -               |
    | B9  | -          | -       | -       | -       | -               |
    | B10 | GND        | GND     | GND     | GND     | GND             |
    | B11 | LAN100.RXN | RXN     | -       | -       | LAN100.RXN      |
    | B12 | LAN100.RXP | RXP     | -       | -       | LAN100.RXP      |
    | B13 | GND        | GND     | GND     | GND     | GND             |
    | B14 | LAN100.TXN | TXN     | -       | -       | LAN100.TXN      |
    | B15 | LAN100.TXP | TXP     |         | -       | LAN100.TXP      |
    | B16 | -          | -       | -       | -       | -               |
    | B17 | EXP_ADR0   | GND     | 3V3     | GND     | EXP_ADDR0       |
    | B18 | EXP_ADR1   | GND     | GND     | 3V3     | EXP_ADDR1       |
    | B19 | XRS_ADR0   | GND     | 3V3     | GND     | XRS_ADDR0       |
    | B20 | XRS_ADR1   | GND     | GND     | 3V3     | XRS_ADDR1       |
       
\#EXT_PWR_DOWN подключен к GPIO42. Активный сигнал низким уровнем.
BACKUP_SD# подключен к GPIO39 через диод. Активный сигнал низким уровнем.
На сигналы RESET_N установлена подтяжка 1.5 кОм к GND.



### IMR_61850 ###

- IMR_61850_v1.1

    | Функция  | Поз. | MDIO  | I2C  | Дополнительно       |
    |:--------:|:-----|:------|:-----|:--------------------|
    | XRS7003  | U4   | var   | var  | XRS7003F            |
    | PHY SE01 | U5   | 0x05  |      | DP83848IVV          |
    | PHY CE01 | U2   | 0x01  |      | 88E1512-A0-NNP2C000 |
    | PHY CE02 | U3   | 0x00  |      | 88E1512-A0-NNP2C000 |
    | I2C_EXP  | U8   |       | var  | PCAL9555AHF,128     |

    | XRS_ADDR1 | XRS_ADDR0 | I2C XRS7003     | MDIO XRS7003  |
    |:----------|:----------|:----------------|:--------------|
    | GND       | GND       | 010_0100 = 0x24 | 0_1000 = 0x08 |
    | GND       | VCC       | 011_0100 = 0x34 | 0_1001 = 0x09 |
    | VCC       | GND       | 110_0100 = 0x64 | 1_1000 = 0x18 |
    | VCC       | VCC       | 111_0100 = 0x74 | 1_1001 = 0x19 |

    | EXP_ADR1 | EXP_ADR0 | ADDRESS I2C_EXP |
    |:---------|:---------|:----------------|
    | GND      | GND      | 010_0000 = 0x20 |
    | GND      | VCC      | 010_0001 = 0x21 |
    | VCC      | GND      | 010_0010 = 0x22 |
    | VCC      | VCC      | 010_0011 = 0x23 |



### Альтернативные функции выводов Raspberry Pi ###

|    | Значение |
|:---|:---------|
| a0 | 4        |
| a1 | 5        |
| a2 | 6        |
| a3 | 7        |
| a4 | 3        |
| a5 | 2        |

Пример использования в */boot/config.txt*:

```
gpio=2=a0
gpio=3=a0
```

Пример использования в *device-tree*:

```
i2c1_pins: i2c1 {
    brcm,pins = <2 3>;
    brcm,function = <4>; /* alt0 */
};
```

Пример настройки в *WiringPi*:

```sh
gpio mode 2 alt0
gpio mode 3 alt0
```



### Разъем Raspberry Pi Compute Module 3+ Lite ###

| Назначение      | RPi CM 3+ Lite | №  | №  | РPi CM 3+ Lite | Назначение      |
|:---------------:|:--------------:|:--:|:--:|:--------------:|:---------------:|
|                 | GND            |  1 |  2 | EMMC_DISABLE_N |                 |
| SDA0            | GPIO0          |  3 |  4 | SDX_VDD        |                 |
| SCL0            | GPIO1          |  5 |  6 | SDX_VDD        |                 |
|                 | GND            |  7 |  8 | GND            |                 |
| SDA1            | GPIO2          |  9 | 10 | SDX_CLK        |                 |
| SCL1            | GPIO3          | 11 | 12 | SDX_CMD        |                 |
|                 | GND            | 13 | 14 | GND            |                 |
|                 | GPIO4          | 15 | 16 | SDX_D0         |                 |
|                 | GPIO5          | 17 | 18 | SDX_D1         |                 |
|                 | GND            | 19 | 20 | GND            |                 |
|                 | GPIO6          | 21 | 22 | SDX_D2         |                 |
| SPI0_CE1_N      | GPIO7          | 23 | 24 | SDX_D3         |                 |
|                 | GND            | 25 | 26 | GND            |                 |
| SPI0_CE0_N      | GPIO8          | 27 | 28 | GPIO28         | SDA0            |
| SPI0_MISO       | GPIO9          | 29 | 30 | GPIO29         | SCL0            |
|                 | GND            | 31 | 32 | GND            |                 |
| SPI0_MOSI       | GPIO10         | 33 | 34 | GPIO30         |                 |
| SPI0_SCLK       | GPIO11         | 35 | 36 | GPIO31         |                 |
|                 | GND            | 37 | 38 | GND            |                 |
|                 | GPIOO0-27_VDD  | 39 | 40 | GPIOO0-27_VDD  |                 |
|                 | **KEY**        |    |    | **KEY**        |                 |
|                 | GPIOO0-27_VDD  | 41 | 42 | GPIOO0-27_VDD  |                 |
|                 | GND            | 43 | 44 | GND            |                 |
| PWM0            | GPIO12         | 45 | 46 | GPIO32         | TXD0/ TXD1      |
| PWM1            | GPIO13         | 47 | 48 | GPIO33         | RXD0/ RXD1      |
|                 | GND            | 49 | 50 | GND            |                 |
| TXD0/ TXD1      | GPIO14         | 51 | 52 | GPIO34         |                 |
| RXD0/ RXD1      | GPIO15         | 53 | 54 | GPIO35         |                 |
|                 | GND            | 55 | 56 | GND            |                 |
| SPI1_CE2_N      | GPIO16         | 57 | 58 | GPIO36         | TXD0            |
| SPI1_CE1_N      | GPIO17         | 59 | 60 | GPIO37         | RXD0            |
|                 | GND            | 61 | 62 | GND            |                 |
| SPI1_CE0_N      | GPIO18         | 63 | 64 | GPIO38         |                 |
| SPI1_MISO       | GPIO19         | 65 | 66 | GPIO39         |                 |
|                 | GND            | 67 | 68 | GND            |                 |
| SPI1_MOSI       | GPIO20         | 69 | 70 | GPIO40         | SPI2_MISO/ TXD1 |
| SPI1_SCLK       | GPIO21         | 71 | 72 | GPIO41         | SPI2_MOSI/ RXD1 |
|                 | GND            | 73 | 74 | GND            |                 |
|                 | GPIO22         | 75 | 76 | GPIO42         | SPI2_CE0_N      |
|                 | GPIO23         | 77 | 78 | GPIO43         | SPI2_CE1_N      |
|                 | GND            | 79 | 80 | GND            |                 |
|                 | GPIO24         | 81 | 82 | GPIO44         | SDA0 / SDA1     |
|                 | GPIO25         | 83 | 84 | GPIO45         | SCL0 / SCL1     |
|                 | GND            | 85 | 86 | GND            |                 |
|                 | GPIO26         | 87 | 88 | HDMI_HPD_N_1V8 |                 |
|                 | GPIO27         | 89 | 90 | EMMC_EN_N_1V8  |                 |
|                 | GND            | 91 | 92 | GND            |                 |
| ...             | ...            | .. | .. | ...            | ...             |



### Разъем Raspberry Pi на отладочной плате XRS7000E ###

| Назначение      | RPi            | №  | №  | РPi            | Назначение      |
|:---------------:|:--------------:|:--:|:--:|:--------------:|:---------------:|
|                 | 3V3            |  1 |  2 | 5V             |                 |
| I2C_SDA         | GPIO2          |  3 |  4 | 5V             |                 |
| I2C_SCL         | GPIO3          |  5 |  6 | GND            |                 |
| MDIO1_CLOCK     | GPIO4          |  7 |  8 | GPIO14         | MDIO1_DATA      |
|                 | GND            |  9 | 10 | GPIO15         |                 |
| INTn            | GPIO17         | 11 | 12 | GPIO18         | RESET_N         |
| POWER_OK        | GPIO27         | 13 | 14 | GND            |                 |
| MDIO2_CLOCK     | GPIO22         | 15 | 16 | GPIO23         | MDIO2_DATA      |