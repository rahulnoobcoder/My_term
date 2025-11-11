#include "app_logic.hpp"
#include "history.hpp"
#include "commands.hpp"
#include <bits/stdc++.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <iostream>
#include <set>

using namespace std;
static bool commandComplete(const vector<string> &lines)
{
    int quotes = 0;
    for (auto &l : lines)
        for (char c : l)
            if (c == '"')
                quotes++;
    return quotes % 2 == 0;
}
// Define global state variables
vector<ShellTab> tabs;
int activeTab = -1;
int tabScrollOffset = 0;
bool should_exit_program = false; // Flag to signal exit from main loop

//username retrieval
static string get_linux_username() {
    const char* user_cstr = getenv("USER");
    if (user_cstr != nullptr && strlen(user_cstr) > 0) {
        return string(user_cstr);
    }
    user_cstr = getlogin();
    if (user_cstr != nullptr && strlen(user_cstr) > 0) {
         return string(user_cstr);
    }
    return "user";
}
string linux_username = get_linux_username();


// --- UI Helper Functions ---
static string trim(const string &s)
{
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}
static int lcsLength(const string &s1, const string &s2)
{
    int m = s1.length();
    int n = s2.length();
    if (m == 0 || n == 0)
        return 0;
    vector<vector<int>> dp(m + 1, vector<int>(n + 1, 0));
    int maxLength = 0;
    for (int i = 1; i <= m; i++)
    {
        for (int j = 1; j <= n; j++)
        {
            if (s1[i - 1] == s2[j - 1])
            {
                dp[i][j] = dp[i - 1][j - 1] + 1;
                if (dp[i][j] > maxLength)
                {
                    maxLength = dp[i][j];
                }
            }
            else
            {
                dp[i][j] = 0;
            }
        }
    }
    return maxLength;
}

// Generates trigrams for the search term.
static set<string> generate_trigrams(const string &str)
{
    set<string> trigrams;
    if (str.length() < 3)
    {
        return trigrams;
    }
    for (size_t i = 0; i <= str.length() - 3; ++i)
    {
        trigrams.insert(str.substr(i, 3));
    }
    return trigrams;
}

static void performHistorySearch(ShellTab &tab)
{
    string searchTerm = trim(tab.searchTerm);
    tab.searchMode = false;
    tab.outputLines.push_back("Search for: '" + searchTerm + "'");

    if (searchTerm.empty())
    {
        return;
    }

    // Check for an exact match
    for (auto it = commandHistory.rbegin(); it != commandHistory.rend(); ++it)
    {
        if (trim(*it) == searchTerm)
        {
            tab.outputLines.push_back("Exact match found:");
            tab.outputLines.push_back(*it);
            return;
        }
    }

    // Generate trigrams for the search term
    set<string> search_trigrams = generate_trigrams(searchTerm);
    if (search_trigrams.empty())
    {
        tab.outputLines.push_back("No significant match found in history (search term too short).");
        return;
    }   

    // Gather candidate commands and score them by trigram matches
    unordered_map<int, int> candidate_scores; // Key: command index, Value: score
    for (const auto &trigram : search_trigrams)
    {
        auto it = historyTrigramIndex.find(trigram);
        if (it != historyTrigramIndex.end())
        {
            for (int cmd_index : it->second)
            {
                candidate_scores[cmd_index]++;
            }
        }
    }

    if (candidate_scores.empty())
    {
        tab.outputLines.push_back("No match found in history.");
        return;
    }

    vector<pair<int, int>> sorted_candidates;
    for (const auto &p : candidate_scores)
    {
        sorted_candidates.push_back(p);
    }
    sort(sorted_candidates.begin(), sorted_candidates.end(), [](const pair<int, int> &a, const pair<int, int> &b)
         { return a.second > b.second; });

    const int MAX_CANDIDATES_TO_CHECK = 100;
    if (sorted_candidates.size() > MAX_CANDIDATES_TO_CHECK)
    {
        sorted_candidates.resize(MAX_CANDIDATES_TO_CHECK);
    }

    // Run the slower but more accurate LCS on the highly-filtered candidates
    int maxLength = 0;
    vector<string> bestMatches;

    for (const auto &candidate : sorted_candidates)
    {
        int cmd_index = candidate.first;
        const string &command = commandHistory[cmd_index];

        int currentLength = lcsLength(command, searchTerm);

        if (currentLength > maxLength)
        {
            maxLength = currentLength;
            bestMatches.clear();
            bestMatches.push_back(command);
        }
        else if (currentLength > 0 && currentLength == maxLength)
        {
            bestMatches.push_back(command);
        }
    }

    if (maxLength > 2)
    {
        tab.outputLines.push_back("Best match(es) (substring length " + to_string(maxLength) + "):");
        for (const auto &match : bestMatches)
        {
            tab.outputLines.push_back(match);
        }
    }
    else
    {
        tab.outputLines.push_back("No significant match found in history.");
    }
}

static string findLCP(const vector<string> &strings)
{
    if (strings.empty())
        return "";
    string lcp = strings[0];
    for (size_t i = 1; i < strings.size(); ++i)
    {
        size_t j = 0;
        while (j < lcp.length() && j < strings[i].length() && lcp[j] == strings[i][j])
        {
            j++;
        }
        lcp.resize(j);
    }
    return lcp;
}

static void handleTabCompletion(ShellTab &tab)
{
    if (tab.input.empty())
        return;

    // Find the word being completed (which could be a path)
    size_t word_end_pos = tab.cursorPos;
    size_t word_start_pos = tab.input.rfind(' ', (word_end_pos > 0) ? word_end_pos - 1 : 0);
    word_start_pos = (word_start_pos == string::npos) ? 0 : word_start_pos + 1;
    string path_to_complete = tab.input.substr(word_start_pos, word_end_pos - word_start_pos);

    // Separate the path into the directory to scan and the prefix to match
    string dir_part = "";
    string prefix = path_to_complete;
    string dir_to_scan = tab.currentDir;

    size_t last_slash = path_to_complete.rfind('/');
    if (last_slash != string::npos)
    {
        dir_part = path_to_complete.substr(0, last_slash + 1);
        prefix = path_to_complete.substr(last_slash + 1);

        if (dir_part[0] == '/')
        { // abs or rel path
            dir_to_scan = dir_part;
        }
        else
        {
            dir_to_scan = tab.currentDir + "/" + dir_part;
        }
    }

    // Find all possible matches in the target directory
    vector<string> matches;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(dir_to_scan.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            string name(ent->d_name);
            if (name == "." || name == "..")
                continue;

            if (name.rfind(prefix, 0) == 0)
            {
                matches.push_back(name);
            }
        }
        closedir(dir);
    }

    if (matches.empty())
        return;

    if (matches.size() == 1)
    {
        string completion = matches[0];
        string fullpath = dir_to_scan + (dir_to_scan.back() == '/' ? "" : "/") + completion;
        struct stat s;

        if (stat(fullpath.c_str(), &s) == 0 && S_ISDIR(s.st_mode))
        {
            completion += "/";
        }
        else
        {
            completion += " ";
        }

        string full_completed_path = dir_part + completion;
        tab.input.replace(word_start_pos, path_to_complete.length(), full_completed_path);
        tab.cursorPos = word_start_pos + full_completed_path.length();
    }
    else
    {
        string lcp = findLCP(matches);
        if (lcp.length() > prefix.length())
        {
            // If there's a common prefix longer than what's typed, complete that part
            string completion_part = lcp.substr(prefix.length());
            tab.input.insert(tab.cursorPos, completion_part);
            tab.cursorPos += completion_part.length();
        }
        else
        {
            tab.completionMode = true;
            tab.completionOptions = matches;
            tab.completionWordStart = word_start_pos;
            tab.completionDirPart = dir_part;
            tab.completionPrefixLength = prefix.length();

            tab.outputLines.push_back(""); // Add a blank line for spacing
            for (size_t i = 0; i < matches.size(); ++i)
            {
                tab.outputLines.push_back(to_string(i + 1) + ". " + matches[i]);
            }
        }
    }
}

void createNewTab(string initial_dir)
{
    ShellTab newTab;
    if (pipe(newTab.parent_to_child_fd) == -1 || pipe(newTab.child_to_parent_fd) == -1)
    {
        perror("pipe failed");
        return;
    }

    newTab.pid = fork();
    if (newTab.pid == -1)
    {
        perror("fork failed");
        return;
    }

    if (newTab.pid == 0)
    { // Child process
        close(newTab.parent_to_child_fd[1]);
        close(newTab.child_to_parent_fd[0]);
        tab_main(newTab.parent_to_child_fd[0], newTab.child_to_parent_fd[1], initial_dir);
        _exit(0);
    }
    else
    { // Parent process
        close(newTab.parent_to_child_fd[0]);
        close(newTab.child_to_parent_fd[1]);
        fcntl(newTab.child_to_parent_fd[0], F_SETFL, O_NONBLOCK);
        newTab.currentDir = initial_dir;
        tabs.push_back(newTab);
        activeTab = tabs.size() - 1;
    }
}

void handle_child_message(const string &msg)
{
    if (activeTab == -1)
        return;

    if (msg.rfind("OUT:", 0) == 0)
    {
        tabs[activeTab].outputLines.push_back(msg.substr(4));
    }
    else if (msg.rfind("CWD:", 0) == 0)
    {
        tabs[activeTab].currentDir = msg.substr(4);
    }
    else if (msg.rfind("CLEAR:", 0) == 0)
    {
        tabs[activeTab].outputLines.clear();
    }
    else if (msg.rfind("PGID:", 0) == 0)
    {
        tabs[activeTab].running_command_pgid = stoi(msg.substr(5));
    }
    else if (msg.rfind("EXIT_PROG:", 0) == 0)
    {
        should_exit_program = true;
    }
    else if (msg.rfind("CMD_DONE:", 0) == 0)
    {
        tabs[activeTab].is_busy = false;
        tabs[activeTab].running_command_pgid = -1;
    }
}

bool handle_x11_event(XEvent &e, X11Context &ctx)
{

    if (should_exit_program)
        return false;
    if (activeTab == -1)
        return true;

    ShellTab &currentTab = tabs[activeTab];

    if (e.type == ConfigureNotify)
    {
        ctx.width = e.xconfigure.width;
        ctx.height = e.xconfigure.height;
    }

    if (e.type == SelectionNotify)
    {
        if (e.xselection.property != None)
        {
            unsigned char *data = NULL;
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;

            XGetWindowProperty(ctx.display, ctx.window, ctx.paste_property_atom, 0, BUFSIZ, True,
                               AnyPropertyType, &actual_type, &actual_format,
                               &nitems, &bytes_after, &data);

            if (data && nitems > 0)
            {
                string pasted_text((char *)data);
                currentTab.input.insert(currentTab.cursorPos, pasted_text);
                currentTab.cursorPos += pasted_text.length();
            }
            if (data)
            {
                XFree(data); // Free the memory 
            }
        }
        return true;
    }

    if (e.type == ButtonPress)
    {
        if (e.xbutton.state & ControlMask) // scrolling through tabs
        {
            if (e.xbutton.button == 4 && tabScrollOffset > 0)
            {
                tabScrollOffset--;
            }
            if (e.xbutton.button == 5 && tabScrollOffset < (int)tabs.size() - 1)
            {
                tabScrollOffset++;
            }
        }
        else if (!(e.xbutton.state & ShiftMask))
        {
            int visibleLines = (ctx.height - (24 * 2 + 5)) / 24;
            int maxScroll = max(0, (int)currentTab.outputLines.size() - visibleLines + 1);
            if (e.xbutton.button == 4 && currentTab.scrollOffset < maxScroll)
                currentTab.scrollOffset++;
            if (e.xbutton.button == 5 && currentTab.scrollOffset > 0)
                currentTab.scrollOffset--;
        }
        else
        {
            // Scroll Up -> Scroll Left
            if (e.xbutton.button == 4 && currentTab.scrollX > 0)
            {
                currentTab.scrollX -= 5; // Scroll 5 characters at a time
                if (currentTab.scrollX < 0)
                    currentTab.scrollX = 0;
            }
            // Scroll Down -> Scroll Right
            if (e.xbutton.button == 5)
            {
                currentTab.scrollX += 5;
            }
        }

        if (e.xbutton.button == 6 && currentTab.scrollX > 0)
        { // Scroll Left
            currentTab.scrollX -= 5;
            if (currentTab.scrollX < 0)
                currentTab.scrollX = 0;
        }
        if (e.xbutton.button == 7)
        { // Scroll Right
            currentTab.scrollX += 5;
        }
    }

    if (e.type == KeyPress)
    {
        KeySym ks = XLookupKeysym(&e.xkey, 0);
        if (currentTab.completionMode)
        {
            if (ks == XK_Return || ks == XK_KP_Enter)
            {
                int choice = -1;
                try
                {
                    if (!currentTab.completionInput.empty())
                    {
                        choice = stoi(currentTab.completionInput);
                    }
                }
                catch (const exception &e)
                {
                    choice = -1;
                }

                if (choice > 0 && choice <= (int)currentTab.completionOptions.size())
                {
                    string selected_file = currentTab.completionOptions[choice - 1];
                    string full_completion_path = currentTab.completionDirPart + selected_file;

                    // Determine the full path to check if it's a directory
                    string path_for_stat;
                    if (full_completion_path[0] == '/')
                    {
                        path_for_stat = full_completion_path;
                    }
                    else
                    {
                        path_for_stat = currentTab.currentDir + "/" + full_completion_path;
                    }

                    struct stat s;
                    if (stat(path_for_stat.c_str(), &s) == 0 && S_ISDIR(s.st_mode))
                    {
                        full_completion_path += "/";
                    }
                    else
                    {
                        full_completion_path += " ";
                    }

                    size_t original_length_to_replace = currentTab.completionDirPart.length() + currentTab.completionPrefixLength;

                    currentTab.input.replace(currentTab.completionWordStart, original_length_to_replace, full_completion_path);
                    currentTab.cursorPos = currentTab.completionWordStart + full_completion_path.length();
                }
                else
                {
                    currentTab.outputLines.push_back("Completion cancelled.");
                }
                currentTab.completionMode = false;
            }
            else if (ks == XK_BackSpace)
            {
                if (!currentTab.completionInput.empty())
                {
                    currentTab.completionInput.pop_back();
                }
            }
            else if (ks == XK_Escape)
            {
                currentTab.outputLines.push_back("Completion cancelled.");
                currentTab.completionMode = false;
            }
            else
            {
                char buf[8];
                Status status;
                int len = Xutf8LookupString(ctx.xic, &e.xkey, buf, sizeof(buf), nullptr, &status);
                if (len > 0 && isdigit(buf[0]))
                {
                    currentTab.completionInput += buf[0];
                }
                else
                {
                    currentTab.outputLines.push_back("Completion cancelled.");
                    currentTab.completionMode = false;
                }
            }

            if (!currentTab.completionMode)
            {
                currentTab.completionInput.clear();
                currentTab.completionOptions.clear();
                currentTab.completionDirPart.clear();
                currentTab.completionPrefixLength = 0;
            }
            return true;
        }

        if (currentTab.searchMode)
        {
            if (ks == XK_Return || ks == XK_KP_Enter)
            {
                performHistorySearch(currentTab);
                return true;
            }
            if (ks == XK_BackSpace)
            {
                if (!currentTab.searchTerm.empty())
                    currentTab.searchTerm.pop_back();
                return true;
            }

            if (ks == XK_Escape)
            {
                currentTab.searchMode = false;
                currentTab.outputLines.push_back("Search cancelled.");
                return true;
            }
            char buf[32];
            Status status;
            int len = Xutf8LookupString(ctx.xic, &e.xkey, buf, sizeof(buf), nullptr, &status);
            if (len > 0)
            {
                currentTab.searchTerm.append(buf, len);
            }
            return true;
        }

        if (e.xkey.state & ControlMask)
        {
            if (ks == XK_t || ks == XK_T)
            {
                createNewTab(currentTab.currentDir);

                return true;
            }
            if ((ks == XK_h || ks == XK_H) && (e.xkey.state & ShiftMask))
            {
                commandHistory.clear();
                clearHistory(); 
                write(tabs[activeTab].parent_to_child_fd[1], "CLEAR_HISTORY_CHILD\1", 20);
                currentTab.outputLines.push_back("Command history cleared.");
                return true;
            }
            if (ks == XK_v || ks == XK_V)
            {
                XConvertSelection(ctx.display, ctx.clipboard_atom, XA_STRING, ctx.paste_property_atom, ctx.window, e.xkey.time);
                return true; 
            }

            if (ks == XK_w || ks == XK_W)
            {
                if (tabs.empty())
                    return true; 


                // Get the tab to close
                int tabToClose = activeTab;
                ShellTab &closingTab = tabs[tabToClose];

                // Tell the child process to exit and clean up pipes
                write(closingTab.parent_to_child_fd[1], "__EXIT__\1", 9);
                close(closingTab.parent_to_child_fd[1]);
                close(closingTab.child_to_parent_fd[0]);
                waitpid(closingTab.pid, NULL, 0);

                // Remove the tab from the vector
                tabs.erase(tabs.begin() + tabToClose);

                // Check if that was the last tab
                if (tabs.empty())
                {
                    activeTab = -1; // Set state to invalid
                    return false;   // Signal the main loop to exit
                }
                else
                {
                    activeTab = max(0, activeTab - 1);
                }

                return true;
            }
            if (ks == XK_c || ks == XK_C)
            {
                if (currentTab.is_busy && currentTab.running_command_pgid != -1)
                {
                    kill(-currentTab.running_command_pgid, SIGINT);

                    const char *interrupt_msg = "__INTERRUPT__\1";
                    write(currentTab.parent_to_child_fd[1], interrupt_msg, strlen(interrupt_msg));

                    if (!currentTab.outputLines.empty())
                    {
                        currentTab.outputLines.back() += " ^C";
                    }
                    currentTab.running_command_pgid = -1; // Prevents sending the signal multiple times
                }
                return true;
            }
            if (ks == XK_z || ks == XK_Z)
            {
                if (currentTab.is_busy && currentTab.running_command_pgid != -1)
                {
                    // Get process info
                    pid_t pgid_to_bg = currentTab.running_command_pgid;
                    string cmd_to_bg = currentTab.running_command_str;

                    // The OS sends SIGTSTP. We send SIGCONT to resume it in the background.
                    kill(-pgid_to_bg, SIGCONT);

                    Job new_job;
                    new_job.id = currentTab.next_job_id++;
                    new_job.pgid = pgid_to_bg;
                    new_job.command = cmd_to_bg;
                    new_job.status = "Running"; 
                    currentTab.jobs.push_back(new_job);
                    currentTab.outputLines.push_back(
                        "[" + to_string(new_job.id) + "]+ " + to_string(new_job.pgid) + " " + cmd_to_bg + " &");

                    currentTab.is_busy = false;
                    currentTab.running_command_pgid = -1;
                    currentTab.running_command_str.clear();
                }
                return true;
            }
            if (ks == XK_r || ks == XK_R)
            {
                currentTab.searchMode = true;
                currentTab.searchTerm.clear();
                return true;
            }
            if (ks == XK_Right)
            {
                activeTab = (activeTab + 1) % tabs.size();

                return true;
            }
            if (ks == XK_Left)
            {
                activeTab = (activeTab - 1 + tabs.size()) % tabs.size();

                return true;
            }
            if (ks == XK_l)
            {
                currentTab.outputLines.clear();
                // write(currentTab.parent_to_child_fd[1], "CLEAR\1", 6);
                return true;
            }

            if (ks == XK_a)
            {
                currentTab.cursorPos = 0;
                return true;
            }
            if (ks == XK_e)
            {
                currentTab.cursorPos = currentTab.input.length();
                return true;
            }
        }

        if (ks == XK_Tab)
        {
            handleTabCompletion(currentTab);
            return true;
        }
        if (ks == XK_Up)
        {
            if (!commandHistory.empty())
            {
                if (currentTab.historyIndex == -1)
                {
                    currentTab.originalInput = currentTab.input;
                    currentTab.historyIndex = commandHistory.size();
                }
                if (currentTab.historyIndex > 0)
                {
                    currentTab.historyIndex--;
                    currentTab.input = commandHistory[currentTab.historyIndex];
                    currentTab.cursorPos = currentTab.input.length();
                }
            }
            return true;
        }
        if (ks == XK_Down)
        {
            if (currentTab.historyIndex != -1)
            {
                if (currentTab.historyIndex < (int)commandHistory.size())
                {
                    currentTab.historyIndex++;
                }
                if (currentTab.historyIndex == (int)commandHistory.size())
                {
                    currentTab.input = currentTab.originalInput;
                    currentTab.historyIndex = -1;
                }
                else
                {
                    currentTab.input = commandHistory[currentTab.historyIndex];
                }
                currentTab.cursorPos = currentTab.input.length();
            }
            return true;
        }
        if (ks == XK_Left)
        {
            if (currentTab.cursorPos > 0)
                currentTab.cursorPos--;
            return true;
        }
        if (ks == XK_Right)
        {
            if (static_cast<size_t>(currentTab.cursorPos) < currentTab.input.size())
                currentTab.cursorPos++;
            return true;
        }
        if (ks == XK_BackSpace)
        {
            if (currentTab.cursorPos > 0)
                currentTab.input.erase(--currentTab.cursorPos, 1);
            return true;
        }
        if (ks == XK_Delete)
        {
            if (static_cast<size_t>(currentTab.cursorPos) < currentTab.input.size())
            {
                currentTab.input.erase(currentTab.cursorPos, 1);
            }
            return true; // This prevents fall-through to the text input block
        }
        if (ks == XK_Return || ks == XK_KP_Enter)
        {
            string prompt = currentTab.multilineBuffer.empty() ? (linux_username + "@my_term:") + currentTab.currentDir + " $ " : "> ";
            currentTab.outputLines.push_back(prompt + currentTab.input);
            currentTab.multilineBuffer.push_back(currentTab.input);

            if (commandComplete(currentTab.multilineBuffer))
            {
                string fullCommand_str;
               
                for (size_t i = 0; i < currentTab.multilineBuffer.size(); ++i)
                {
                    fullCommand_str += currentTab.multilineBuffer[i];
                    if (i < currentTab.multilineBuffer.size() - 1)
                    {
                        fullCommand_str += '\n';
                    }
                }

                string trimmed_command = trim(fullCommand_str);
                if (trimmed_command == "jobs")
                {
                    for (int j = currentTab.jobs.size() - 1; j >= 0; --j)
                    {
                        auto &job = currentTab.jobs[j];
                        
                        // Print the job's current status
                        currentTab.outputLines.push_back(
                            "[" + to_string(job.id) + "]+ " + job.status + "    " + job.command);

                        // If "Done" job, remove it so it doesn't appear again.
                        if (job.status == "Done")
                        {
                            currentTab.jobs.erase(currentTab.jobs.begin() + j);
                        }
                    }
                }
                else if (trimmed_command.rfind("kill %", 0) == 0)
                {
                    try
                    {
                        int job_id_to_kill = stoi(trim(trimmed_command.substr(6)));
                        bool found = false;
                        for (auto it = currentTab.jobs.begin(); it != currentTab.jobs.end(); ++it)
                        {
                            if (it->id == job_id_to_kill)
                            {
                                // Send SIGTERM to terminate the process group.
                                kill(-(it->pgid), SIGTERM);
                                currentTab.outputLines.push_back("Terminated: [" + to_string(it->id) + "] " + it->command);
                                // The main loop will automatically reap and remove the job.
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            currentTab.outputLines.push_back("kill: job not found: %" + to_string(job_id_to_kill));
                        }
                    }
                    catch (const exception &e)
                    {
                        currentTab.outputLines.push_back("kill: invalid job id");
                    }
                }
                else if (!trimmed_command.empty())
                {
                    saveHistory(trimmed_command);

                    // Prepare the command with a delimiter and SEND IT to the child process.
                    string command_to_send = trimmed_command + "\1";
                    write(currentTab.parent_to_child_fd[1], command_to_send.c_str(), command_to_send.length());

                    // Set the tab's state to "busy" waiting for the command to finish.
                    currentTab.is_busy = true;
                    currentTab.running_command_str = trimmed_command;
                }
                currentTab.multilineBuffer.clear();
                currentTab.input.clear();
                currentTab.cursorPos = 0;
                currentTab.historyIndex = -1;
            }
            else
            {
                currentTab.input.clear();
                currentTab.cursorPos = 0;
            }
            return true;
        }

        char buf[32];
        Status status;
        int len = Xutf8LookupString(ctx.xic, &e.xkey, buf, sizeof(buf), nullptr, &status);

        if (len > 0)
        {
            currentTab.historyIndex = -1;
            currentTab.input.insert(currentTab.cursorPos, buf, len);
            currentTab.cursorPos += len;
        }
    }
    return true;
}