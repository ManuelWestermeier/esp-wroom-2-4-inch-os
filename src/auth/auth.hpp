#pragma once

#include <Arduino.h>
#include <vector>
#include <string>

#include "utils/crypto.hpp"
#include "fs/enc-fs.hpp"
#include "io/read-string.hpp"
#include "screen/index.hpp"
#include "utils/rect.hpp"
#include "utils/time.hpp"

namespace Auth
{
    extern String username;
    extern String password;

    // Check if a user exists by hashed directory
    bool exists(const String &user);

    // Attempt login with username and password
    bool login(const String &user, const String &pass);

    // Create new account, returns false if user already exists
    bool createAccount(const String &user, const String &pass);

    // Main login/create account screen
    void init();
}
