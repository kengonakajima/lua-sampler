#include "assert.h"
#include "lua.h"
#include "lauxlib.h"

#include <strings.h>


static int pure_debug(lua_State* L)
{
    int n=0;
    lua_createtable(L,n,0);
    
    int i;
    for(i=0;i< n;i++){
        double val = rand() / 32768.0;
        lua_pushnumber(L,val);
        assert( lua_type(L,-2) == LUA_TTABLE );
        lua_rawseti(L,-2,i+1);
    }
    
    return 1;
}


static const luaL_reg pure_funcs[] = {
    {"debug", pure_debug},
    {NULL,NULL}
};

LUALIB_API int luaopen_pure ( lua_State *L ) {

    lua_newtable(L);
    luaL_register(L,NULL, pure_funcs );
    return 1;
}
