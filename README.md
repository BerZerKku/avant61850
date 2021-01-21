[TOC]

## TODO

- [ ] Синхронизировать настройки fcmd в папках etc и var.
- [ ] Сделать драйвер PL011 в виде модуля, либо вообще убрать его из ядра.
- [ ] В рабочую версию образа в cmdline.txt добавить loglevel=4 (KERN_WARNING) или 3 (KERN_ERR).
- [ ] Разобраться какие файлы необходимо хранить в ядре, для компиляции модулей xrs
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