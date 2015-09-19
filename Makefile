ifneq ($(KERNELRELEASE),)
    obj-m	:= SMT_schedule.o

    else
    KDIR	:= /lib/modules/$(shell uname -r)/build
    PWD		:= $(shell pwd) 
    INCLUDE     :=/home/krishna/linux-src/linux-3.2.0/arch/x86/include/asm

    default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
    clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
    endif
