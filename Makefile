CFLAGS += -std=c11 -pedantic -Wall -Wextra
LDFLAGS += -lxcb -lxcb-keysyms -lxcb-randr

DEBUG = 0
ifeq (${DEBUG},0)
   CFLAGS  += -Os
else
   CFLAGS  += -g
endif

4wm :
	gcc ${CFLAGS} ${LDFLAGS} 4wm.c -o 4wm

clean :
	rm -f 4wm
