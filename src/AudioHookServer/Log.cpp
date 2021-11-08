#include "Log.h"
#include <fstream>
#include <iostream>

LogLevel LogMinLevel = (LogLevel)(-1);

void WriteLogRaw(std::string_view msg)
{
    if (std::cout.good()) {
        std::cout << msg << std::endl;
    }
}