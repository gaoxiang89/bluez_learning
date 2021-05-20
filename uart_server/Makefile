
LIBS=-L/usr/lib -lpthread -lgio-2.0 -lglib-2.0 -lc -lz -lm -lpcre -lgobject-2.0 -lgmodule-2.0 -lffi
CC_FLAG=-Wall -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include/ 
CC=gcc

OBJ_NAME=uart_server

SRC=main.c advertising.c log.c gatt.c uart_server.c adapter.c

all : $(OBJ_NAME)

$(OBJ_NAME) : $(SRC)
	$(CC) $(SRC) $(LIBS) $(CC_FLAG) -O2 -o $@
	
	
.PHONY : clean

clean :
	rm -rf $(OBJ_NAME)

