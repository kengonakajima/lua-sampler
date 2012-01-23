#osx only

CC=gcc 
OUT=luasampler.so

LUA=lua/src/lua
LIBLUA=lua/src/liblua.a

CATEST=a.out

all: $(OUT) pure $(CATEST)

$(CATEST): ca.c
	$(CC) -framework Foundation -framework CoreAudio -framework AudioToolbox ca.c 

$(OUT): luasampler.o $(LIBLUA)
	$(CC)  -o $(OUT) luasampler.o -dynamiclib $(LIBLUA) -framework Foundation -framework AudioToolbox 

pure : pure.c $(LIBLUA)
	$(CC)  -o pure.so pure.c -dynamiclib $(LIBLUA) 


luasampler.o: luasampler.c
	$(CC) -c luasampler.c -g 


$(LIBLUA):
	git clone http://repo.or.cz/r/lua.git
	cd lua/; make macosx

test:
	$(LUA) test.lua

clean:
	rm -rf *.o *.so lua

