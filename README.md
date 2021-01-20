[TOC]

## TODO

- [ ] Синхронизировать настройки fcmd в папках etc и var.
- [ ] Сделать драйвер PL011 в виде модуля, либо вообще убрать его из ядра.


## Возможные ошибки

- make: dtc: Command not found
  ```sh
  sudo apt install device-tree-compiler
  ```

- fatal error: curses.h: No such file or directory
  ```sh
  sudo apt install libncurses5-dev libncursesw5-dev
  ```