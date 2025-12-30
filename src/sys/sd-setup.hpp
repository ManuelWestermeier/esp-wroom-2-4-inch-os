#pragma once

#include <Arduino.h>

#include "../fs/index.hpp"

void sdSetup()
{
    SD_FS::init();
    SD_FS::lsDirSerial("/");
}