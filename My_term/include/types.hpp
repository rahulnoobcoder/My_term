#pragma once

#include <string>
#include <vector>
#include <sys/types.h>
using namespace std;
struct Job {
    int id;
    pid_t pgid;
    string command;
    string status;
};

// Represents the state of a single terminal tab
struct ShellTab {
    // Process management
    pid_t pid = -1;
    int parent_to_child_fd[2] = {-1, -1};
    int child_to_parent_fd[2] = {-1, -1};

    // UI state for drawing
    vector<string> outputLines;
    string input;
    vector<string> multilineBuffer;
    int scrollOffset = 0;
    int scrollX = 0;
    int cursorPos = 0;
    string currentDir;

    // Interactive features
    bool searchMode = false;
    string searchTerm;
    int historyIndex = -1;
    string originalInput;

    //  COMPLETION STATE for Numbered List UI 
    bool completionMode = false;
    vector<string> completionOptions;
    size_t completionWordStart = 0;
    string completionDirPart;      // Stores the directory part of the path
    size_t completionPrefixLength = 0;  // Stores length of the filename prefix
    string completionInput;

    // Job control
    bool is_busy = false;
    pid_t running_command_pgid = -1;
    string running_command_str;
    vector<Job> jobs;
    int next_job_id = 1;
};