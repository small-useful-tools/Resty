#pragma once

#include "Rule.h"

namespace resty
{
AppSettings LoadSettings();
void SaveSettings(const AppSettings& settings);
}
