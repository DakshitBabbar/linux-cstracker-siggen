obj-m += siggen.o

all:
	make -C ~/Desktop/OS/linux-6.1.71 M=$(PWD)

install:
	sudo make -C ~/Desktop/OS/linux-6.1.71 M=$(PWD) modules_install INSTALL_MOD_PATH=/media/dakshit/platform