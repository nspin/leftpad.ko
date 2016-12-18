obj-m += leftpad.o
MY_CFLAGS += -g
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}

all:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) modules

debug:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) modules
    EXTRA_CFLAGS="$(MY_CFLAGS)"

clean:
	make -C $(dev)/lib/modules/4.4.36/build M=$(PWD) clean
