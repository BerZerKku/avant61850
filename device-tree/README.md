*Образ Raspberry Pi* 
====================

### Описание ###

Загрузочный раздел и настройки корневой системы *Raspberry Pi CM 3+* на блоке BVP_61850_v2.2 с модулем IMR_61850_v1.1.

За основу взято *device-tree* и *boot* с образа XRS7004E-FVM-V1.0.1 для отладочной платы ARWSCBRD-XRS7004E.

Для настройки свежего образа необходимо запустить скрипты */boot/other/0x_install.sh* по пордяку с перезагрузкой после каждого. 



### TODO ###

- [ ] Добавить в скрипт запуск ПО на изолированных ядрах
- [ ] Проверить работу uart0
- [ ] Добавить файл с версией 
- [ ] Проверить настройку временной зоны
- [ ] Добавить установку или инструкцию для *chroot-Raspbian* (/var/raspbian10)
- [ ] При использовании reset в xrs_guard или xrs@0 драйвера загружаются но прием/передача не идут. 
    Сигнал *PWR_DOWN/INT* на микросхеме U5 *DP83848* при этом всегда в 0. Соответственно INTn тоже.
- [ ] Убрать системные сообщения *NOHZ: local_softirq_pending 08* при старте системы
    Появлились в коммите 22.12.2020



### Замечания ###

- **С оригинальным загрузчиком не загружаются overlays (?!).**
- **С оригинальным загрузчиком плавает частота у uart (?!).**
- **Неусточйчивая работа через отладочный ethernet при работе портов резервирования.**
- **Консоль на 14/15 не всегда доходит до ввода логина, если модуль XRS не загрузился.**
- **Если использовать Compute Module Board Plus + модуль IMR_61850_v1.0 на плате BVP_61850_v2.0, надо в RPi вставить USB-Ethernet преобразователь. Иначе на этапе загрузки не успевает (?!) грузится eth0.** 
- Тип используемой микросхемы XRS определяется один раз, при первом включении. Чтобы запустить алгоритм снова, необходимо создать файл /etc/xrs-first-boot. Сам алгоритм находится в скрипте /var/lib/xr7/rpi/script/drivers.init.



### Изменения ###

#### Ядро ####

Ядро из сборки #10 (ревизия svn r15):
- добавлена поддержка RTC и модуль для DS3231. 
- добавлена поддержка последовательного порта ttyAMA0.0

> **conf/raspberry-rpi2.config конфиг с котрым было собрано ядро.**

##### Сборка ядра #####

- Сборка запускается командой `make`, полученные файлы *.dtb помещаются в папку *boot/*.

- Для сборки должен быть установлен компилятор *device-tree*:
    ```sh
    sudo apt install device-tree-compiler
    ```

##### Использование overlay #####

> **Смотри замечания!**

Для применения файлов *overlay* необходимо добавить их в */boot/config.txt*:

```
dtoverlay=bvp61850_2v0
dtoverlay=imr61850_1v0
```

#### */boot* ####

- Убрано все лишнее.
- Загрузчик скопирован из сборки *2019-09-26-raspbian-buster-lite.img*.
    ```
    /boot/bootcode.bin
    /boot/fixup.dat
    /boot/start.elf
    ```
- Указаны ядро и дерево для загрузки.
    ```
    /boot/config.txt:

    kernel=kernel7.img
    device_tree=bcm2710-rpi-3-b-xrs7003f-bvp-2v2-imr-1v1.dtb
    ```
- Включены i2c0 и ds3231 (для последнего добавлен overlay).
    ```
    /boot/config.txt:

    dtparam=i2c0=on
    dtoverlay=i2c-rtc,ds3231
    ```
- Включен uart0 (*/dev/ttyAMA0*).
    ```
    /boot/config.txt:

    core_freq=250
    enable_uart=1
    ```
- Поданы сигналы сброса для модулей *IM1* и *IM2*.
    ```
    /boot/config.txt:

    gpio=44,45=op,pd
    ```
- Добавлены два изолированных ядра. 
    ```
    /boot/cmdline.txt:

    ... isolcpus=2,3 ...
    ```

#### *rootfs* ####

- Установлен raspi-config.
- Отключен вывод отладочной информации в последовательный порт и USB.
    ```
    /etc/inittab:

    #T0:23:respawn:/sbin/getty -L ttyAMA0 115200 vt100
    #U0:23:respawn:/sbin/getty -L ttyUSB0 115200 vt100
    ```
    Добавлена возможность раоботы из под *root* по последовательному порту и USB.
    ```
    /etc/securetty:

    ttyAMA0
    ttyUSB0
    ```
- Установленна временная зона.
    ```
    /etc/timezone:

    Europe/Moscow
    ```
- Добавлена загрузка *i2c* модуля, для подключения устройств */dev/i2c-x*.
    ```
    /etc/modules:

    i2c-dev
    ```
- Добавлена возможнос
- Исправлены источники пакетов.
    ```
    /etc/apt/sources.list:

    deb http://archive.debian.org/debian wheezy main
    deb http://archive.raspberrypi.org/debian wheezy main 
    ```
- Исправлены настройки для работы с RTC 
    ```
    /etc/default/hwclock:

    HWCLOCKACCESS=no
    ```
    и добавлена синхронизация при загрузке.
    ```
    /etc/udev/rules.d/85-hwclock.rules:

    KERNEL=="rtc0", RUN+="/sbin/hwclock --rtc=$root/$name --hctosys"
    ```
- Добавлены настройки для сетевых интерфейсов
    ```
    /etc/network/interfaces:

    auto net1
    iface net1 inet static
    	address 10.0.0.111
        netmask 255.255.255.0
        gateway 10.0.0.1   

    auto home
    allow-hotplugin home
    iface home inet static
        address 192.168.137.111
        netmask 255.255.255.0
        gateway 192.168.137.1
    ```
    и адрес *dns-сервера*
    ```
    /etc/resolv.conf:

    nameserver 8.8.8.8
    ```
    Сетевым интерфейсам присовены постоянные имена.
    ```
    /etc/udev/rules.d/10-network.rules:

    SUBSYSTEM=="net", DEVPATH=="/devices/platform/*usb/usb1/1-1/1-1.1/*", NAME="net1"
    SUBSYSTEM=="net", DEVPATH=="/devices/platform/*usb/usb1/1-1/1-1.2/*", NAME="xf1"
    SUBSYSTEM=="net", DEVPATH=="/devices/platform/*usb/usb1/1-1/1-1.3/*", NAME="xf2"
    SUBSYSTEM=="net", DEVPATH=="/devices/platform/*usb/usb1/1-1/1-1.4/*", NAME="xf3"
    SUBSYSTEM=="net", DEVPATH=="/devices/platform/*usb/usb1/1-1/1-1.5/"", NAME="home"
    ```
    *net1 - отладочный порт подключенный к USB-HUB.*
    *home - отладочный порт для преобразователя USB-Ethernet.*
- Добавлена цветовая настройка *bash*.
    ```
    /root/.bashrc
    /root/.profile
    ```
- Добавлен скрипты для автоматической загрузки приложений и ручного запуска chroot.
    ```
    /root/auto.sh
    /root/manual.sh
    /root/ch-mount.sh
    ```
    ```sh
    /etc/rc.local:

    ROOTFS=/var/raspbian10
    /root/ch-mount.sh -m $ROOTFS
    sleep 10 && /root/auto.sh &
    ```
    **Запуск скрипта задержан на 10 секунд чтобы успели загрузиться все сетевые интерфейсы и закончилась настройка резервирования.**


### XRS ###


#### /var/lib/xr7/rpi/script/system.init ####

Используются ключи *start*, *stop* и *restart*. Вызывает *drivers.init*.

#### /var/lib/xr7/rpi/script/drivers.init ####

Проверка наличия необходимых драйверов и их запуск.

- Убрана проверка наличия файлов *device-tree* в функции *set_hw_type()*.
- Исправлено название интерфейса для работы с модулем в разъеме *XF1*.
    ```sh
    ip link set xf1 up
    ```
- Отключена фильтрация *multicast* кадров.
    ```sh
    insmod $FLX_DRIVER_PATH/flx_frs.ko ipo=1
    ```

#### /var/lib/xr7/rpi-xrs7003/fcmd/\*/factory ####

- *ethernet* : настройка интерфейсов *CExx*.
- *ip* : настройка интерфейсов *SExx*.
- *redundancy_supervision* : настройка протоколов резервирования.
- *routing* :
- *switching* : настройка коммутаторов.


#### Добавление новых интерфейсных модулей ####

Скрипты */var/lib/xr7/rpi/script/*:
- Добавить запуск интерфейсов *SExx* в *system.init/start()*.
- Добавить настройку задержек для работы *PTP* в *drivers.init/setup_delay_files()*.
- Добавить запуск интерфейсов *xf* в *drivers.init/start()* и их останов *stop()*.
- Добавить интерфейсы *SExx* и *CExx* в настройку MTU *drivers.init/start()*.

Настройки */var/lib/xr7/rpi-xrs7003/fcmd/*:
- [fcmd](#/var/lib/xr7/rpi-xrs7003/fcmd/\*/factory) 

#### Установка времени ####


### CHROOT ###

#### /var/raspbian10 ####

Дополнительно установлено:
- wiringpi
	Работа с портами ввода-вывода.