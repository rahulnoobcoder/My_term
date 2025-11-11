#pragma once

#include <string>
#include <vector>
#include <unordered_map> 
using namespace std;
// Global history, accessible via this module
extern vector<string> commandHistory;
// The inverted index for fast searching. Maps a trigram to a list of command indices.
extern unordered_map<string, vector<int>> historyTrigramIndex;

extern const size_t MAX_HISTORY_FILE;
extern const size_t MAX_HISTORY_DISPLAY;

void loadHistory();
void saveHistory(const string &cmd);
void clearHistory();

void buildHistoryIndex();
