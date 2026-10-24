#ifndef MM3_LUA_NATIVES_H
#define MM3_LUA_NATIVES_H
/* Guest addresses of the Lua natives src/mm3_lua_natives.c adds: inside
 * sub_0020646F's body, which nothing calls; mm3_runtime_entries.c maps them. */
#define MM3_LUA_AI_ALL_CARS_VA 0x0020647Fu
void mm3_lua_ai_all_cars(void);
#endif
