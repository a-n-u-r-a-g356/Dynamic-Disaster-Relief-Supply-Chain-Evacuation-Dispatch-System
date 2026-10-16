#pragma once
#include "engine.h"
#include <istream>

// Interactive command shell. Reads commands from `in` until EOF or "quit".
int runShell(Model &m, std::istream &in, bool showPrompt);
