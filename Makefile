ifneq ($(KERNELRELEASE),)
	obj-m := fifo_module.o
else
all:
	$(MAKE) -C /lib/modules/`uname -r`/build M=`pwd` modules
	gcc fifo_app.c -o fifo_app.out
clean:
	$(MAKE) -C /lib/modules/`uname -r`/build M=`pwd` clean
	rm -f *~ *.out
endif
