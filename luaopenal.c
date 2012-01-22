

#include "lua.h"
#include "lauxlib.h"



static const luaL_reg luaopenal_f[] = {
    //    {"pack", msgpack_pack_api },
    //    {"unpack", msgpack_unpack_api },
    //    {"largetbl", msgpack_largetbl },
    {NULL,NULL}
};

LUALIB_API int luaopen_luaopenal ( lua_State *L ) {    
    lua_newtable(L);
    luaL_register(L,NULL, luaopenal_f );
    return 1;
}
