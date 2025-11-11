#include "history.hpp"
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <set>
using namespace std;
// global variables
vector<string> commandHistory;
unordered_map<string, vector<int>> historyTrigramIndex;

const size_t MAX_HISTORY_FILE = 10000;
const size_t MAX_HISTORY_DISPLAY = 1000;
string historyFile = ".bat_term_history";



static set<string> generate_trigrams(const string& str) {
    set<string> trigrams;
    if (str.length() < 3) {
        return trigrams;
    }
    for (size_t i = 0; i <= str.length() - 3; ++i) {
        trigrams.insert(str.substr(i, 3));
    }
    return trigrams;
}

void buildHistoryIndex() {
    historyTrigramIndex.clear();
    for (size_t i = 0; i < commandHistory.size(); ++i) {
        set<string> trigrams = generate_trigrams(commandHistory[i]);
        for (const auto& trigram : trigrams) {
            historyTrigramIndex[trigram].push_back(i);
        }
    }
}



static string trim(const string &s)
{
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

void loadHistory()
{
    commandHistory.clear();
    ifstream file(historyFile);
    string line;
    while (getline(file, line))
    {
        if (!line.empty())
            commandHistory.push_back(line);
    }
    if (commandHistory.size() > MAX_HISTORY_FILE)
        commandHistory.erase(commandHistory.begin(), commandHistory.end() - MAX_HISTORY_FILE);
    buildHistoryIndex();
}

void saveHistory(const string &cmd)
{
    string trimmed = trim(cmd);
    if (trimmed.empty() || trimmed == "history")
        return;
    commandHistory.push_back(trimmed);

    if (commandHistory.size() > MAX_HISTORY_FILE){
        commandHistory.erase(commandHistory.begin(), commandHistory.end() - MAX_HISTORY_FILE);
        buildHistoryIndex();
    }
    else{
        int new_index = commandHistory.size() - 1;
        set<string> trigrams = generate_trigrams(trimmed);
        for (const auto& trigram : trigrams) {
            historyTrigramIndex[trigram].push_back(new_index);
        }
    }
    ofstream file(historyFile);
    for (const auto &s : commandHistory)
        file << s << "\n";
}

void clearHistory()
{
    commandHistory.clear(); // Clear the in-memory vector
    historyTrigramIndex.clear(); 
    ofstream file(historyFile, ofstream::out | ofstream::trunc);
    file.close();
}