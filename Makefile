ifneq ($(KERNELRELEASE),)
	obj-m := stopwatch.o
else
all:
	$(MAKE) -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	$(MAKE) -C /lib/modules/`uname -r`/build M=`pwd` clean
	rm -f *~ *.out
endif
