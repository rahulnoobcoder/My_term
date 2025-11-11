#pragma once

#include "types.hpp"
#include "x11.hpp"
using namespace std;
// Global application state, managed by this module
extern vector<ShellTab> tabs;
extern int activeTab;
extern int tabScrollOffset;
extern string linux_username;

void createNewTab(string initial_dir);
void handle_child_message(const string& msg);

bool handle_x11_event(XEvent &e, X11Context &ctx);