*/xr7-drivers* 
====================



Драйвера для работы с XRS700x. Взяты с образа *XRS7004E-FVM-V1.0.1*.



### Сборка ###

- Сборка драйверов `make.sh`.
	Полученные модули будут помещены в *./drivers*.

- Сборка драйверов с выводом отладочной информации `make.sh --debug`.
	Полученные модули будут помещены в */drivers_debug*.



### Установка ###

Модули необходимо скопировать в соответствующую ядру папку, например */var/lib/xr7/default/drivers/3.18.11-rt6*. 



### Ядро ###

Путь к исходникам ядра задается в *make.sh*, переменная *KDIR*.
