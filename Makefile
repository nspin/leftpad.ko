obj-m += leftpad.o

all:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) modules

clean:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) clean
