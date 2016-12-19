obj-m += leftpad.o

all:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) modules

debug:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) modules
	EXTRA_CFLAGS="-DLEFTPAD_DEBUG -g"

clean:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) clean
