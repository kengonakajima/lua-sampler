#osx only

CC=gcc 
OUT=luaopenal.so

LUA=lua/src/lua
LIBLUA=lua/src/liblua.a

CATEST=a.out

all: $(OUT) $(CATEST)

$(CATEST): ca.c
	$(CC) -framework Foundation -framework CoreAudio -framework AudioToolbox ca.c

$(OUT): luaopenal.o $(LIBLUA)
	$(CC) -o $(OUT) luaopenal.o -dynamiclib $(LIBLUA)

luaopenal.o: luaopenal.c
	$(CC) -c luaopenal.c -g


$(LIBLUA):
	git clone http://repo.or.cz/r/lua.git
	cd lua/; make macosx

test:
	$(LUA) test.lua

clean:
	rm -rf *.o *.so lua

