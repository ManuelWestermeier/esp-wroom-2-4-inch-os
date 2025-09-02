#include "fslib.hpp"

namespace LuaApps
{
    namespace FsLib
    {

        // ---------------- FS_get ----------------
        // Lua: FS_get(key) -> string / nil
        int lua_FS_get(lua_State *L)
        {
            const char *key = luaL_checkstring(L, 1);
            String appId = getApp(L)->path; // get current app path/id

            ENC_FS::Buffer data = ENC_FS::Storage::get(appId, key);

            if (data.empty())
            {
                lua_pushnil(L);
            }
            else
            {
                lua_pushlstring(L, reinterpret_cast<const char *>(data.data()), data.size());
            }
            return 1;
        }

        // ---------------- FS_set ----------------
        // Lua: FS_set(key, data) -> bool
        int lua_FS_set(lua_State *L)
        {
            const char *key = luaL_checkstring(L, 1);
            size_t len = 0;
            const char *dataPtr = luaL_checklstring(L, 2, &len);

            String appId = getApp(L)->path; // current app id/path

            ENC_FS::Buffer data(dataPtr, dataPtr + len);
            bool ok = ENC_FS::Storage::set(appId, key, data);

            lua_pushboolean(L, ok);
            return 1;
        }

        int lua_FS_del(lua_State *L)
        {
            const char *key = luaL_checkstring(L, 1);

            String appId = getApp(L)->path; // current app id/path

            bool ok = ENC_FS::Storage::del(appId, key);

            lua_pushboolean(L, ok);
            return 1;
        }

        // ---------------- Register Functions ----------------
        void register_fs_functions(lua_State *L)
        {
            lua_register(L, "FS_get", lua_FS_get);
            lua_register(L, "FS_set", lua_FS_set);
            lua_register(L, "FS_del", lua_FS_del);
        }

    } // namespace FsLib
} // namespace LuaApps
