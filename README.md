[TOC]

## TODO

- [ ] Добавить в скрипты build.sh зависисмости make / install / clean.

!image:
- [ ] Синхронизировать настройки fcmd в папках etc и var.
- [ ] В рабочую версию образа в cmdline.txt добавить loglevel=4 (KERN_WARNING) или 3 (KERN_ERR).

linux-raspbian-3.18.11:
- [ ] Сделать драйвер PL011 в виде модуля.

xr7-drivers:
- [x] Исправить алгоритм определения модели Raspberry в драйвере uart.
- [ ] Разобраться с используемыми командами в драйвере tty* ioctl
- [ ] Добавить в драйвера tty* настройку параметров termios
- [ ] Проверить IRQ mini Uart на Raspberry 3, 3+, 4 и разных ядрах. При необходимости учесть в ttyUart1
- [ ] Добавить в драйвера ttyUart1 прием и передачу с использованием FIFO
- [ ] Разобраться какие файлы необходимо хранить в ядре, для компиляции модулей xrs.
- [ ] Вынести одинаковую часть драйверов tty* в отдельный файл
  ```
  ERROR: Kernel configuration is invalid.
  include/generated/autoconf.h or include/config/auto.conf are missing.

  Run 'make oldconfig && make prepare' on kernel src to fix it.
  WARNING: Symbol version dump ./Module.symvers
  is missing; modules will have no dependencies and modversions.
  ```


## Возможные ошибки

- make: dtc: Command not found
  ```sh
  sudo apt install device-tree-compiler
  ```

- fatal error: curses.h: No such file or directory
  ```sh
  sudo apt install libncurses5-dev libncursesw5-dev
  ```