#include "commands.hpp"
#include "history.hpp"
#include <bits/stdc++.h>
#include <glob.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/prctl.h>

using namespace std;

static string trim(const string &s)
{
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}
static volatile sig_atomic_t mw_controller_stop = 0;

void mw_controller_sigint_handler(int signo) {
    (void)signo;
    mw_controller_stop = 1;
}

static vector<char *> parseCommand(const string &cmd)
{
    vector<char *> args;
    string arg;
    bool inQuotes = false;

    for (size_t i = 0; i < cmd.size(); i++)
    {
        char c = cmd[i];

        if (c == '\\')
        {
            // This is an escape character
            if (i + 1 < cmd.size())
            {
                
                i++;
                // Add the escaped character to the arg literally
                arg.push_back(cmd[i]);
            }
            
        }
        else if (c == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (isspace(c) && !inQuotes)
        {
            // This is a space, and we're not in quotes.
            if (!arg.empty())
            {
                if (arg.find('*') != string::npos || arg.find('?') != string::npos)
                {
                    glob_t results;
                    if (glob(arg.c_str(), GLOB_NOCHECK, NULL, &results) == 0)
                        for (size_t j = 0; j < results.gl_pathc; j++)
                            args.push_back(strdup(results.gl_pathv[j]));
                    globfree(&results);
                }
                else
                    args.push_back(strdup(arg.c_str()));
                arg.clear();
            }
        }
        else
        {
            // Any other normal character
            arg.push_back(c);
        }
    }

    // Add the final argument if it exists
    if (!arg.empty())
    {
        if (arg.find('*') != string::npos || arg.find('?') != string::npos)
        {
            glob_t results;
            if (glob(arg.c_str(), GLOB_NOCHECK, NULL, &results) == 0)
                for (size_t j = 0; j < results.gl_pathc; j++)
                    args.push_back(strdup(results.gl_pathv[j]));
            globfree(&results);
        }
        else
            args.push_back(strdup(arg.c_str()));
    }

    args.push_back(nullptr);
    return args;
}

static pid_t launch_pipeline(vector<string> parts, const string &cwd, int &read_fd)
{
    int n = parts.size();
    if (n == 0)
        return -1;

    // Create n-1 pipes for the pipeline
    int final_output_pipe[2];
    if (pipe(final_output_pipe) < 0)
    {
        perror("launch_pipeline: final output pipe failed");
        return -1;
    }
    vector<array<int, 2>> pipes(n - 1);
    for (int i = 0; i < n - 1; ++i)
    {
        if (pipe(pipes[i].data()) < 0)
        {
            perror("launch_pipeline: intermediate pipe failed");
            return -1;
        }
    }

    pid_t pgid = -1;

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("launch_pipeline: fork failed");
            return -1;
        }

        if (pid == 0)
        { // Child Process
            if (i == 0)
                setpgid(0, 0);
            else
                setpgid(0, pgid);

            if (chdir(cwd.c_str()) != 0)
                _exit(127);

            // Set up stdin
            if (i > 0)
            {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Set up stdout
            if (i < n - 1)
            {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            else
            {
                dup2(final_output_pipe[1], STDOUT_FILENO);
                dup2(final_output_pipe[1], STDERR_FILENO); 
            }

            close(final_output_pipe[0]);
            close(final_output_pipe[1]);
            for (auto &p : pipes)
            {
                close(p[0]);
                close(p[1]);
            }

            vector<char *> args = parseCommand(parts[i]);
            execvp(args[0], args.data());
            perror("execvp failed");
            _exit(127);
        }

        if (i == 0)
            pgid = pid; // The PGID of the pipeline is the PID of the first process.
    }

    
    close(final_output_pipe[1]); // Close the write end
    for (auto &p : pipes)
    {
        close(p[0]);
        close(p[1]);
    }

    // Return the read end of the final pipe to the caller
    read_fd = final_output_pipe[0];
    return pgid;
}

static void execute_pipeline(const string& command_string, const string& cwd) {
    // Split the command string by pipes '|'
    vector<string> parts;
    string part;
    stringstream ss(command_string);
    while (getline(ss, part, '|')) {
        parts.push_back(trim(part));
    }

    // If there's no pipe, it's a simple command.
    if (parts.size() <= 1) {
        vector<char*> args = parseCommand(command_string);
        if (args.empty() || args[0] == nullptr) _exit(1);
        execvp(args[0], args.data());
        perror("execvp failed"); 
        _exit(127);
    }

    // pipeline with 2 or more parts
    int in_fd = STDIN_FILENO; // The first command reads from standard input

    for (size_t i = 0; i < parts.size() - 1; ++i) {
        int p[2];
        if (pipe(p) < 0) {
            perror("pipe failed in execute_pipeline");
            _exit(1);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed in execute_pipeline");
            _exit(1);
        }

        if (pid == 0) { 
            if (in_fd != STDIN_FILENO) {
                dup2(in_fd, STDIN_FILENO); // Set input to the read-end of the previous pipe
                close(in_fd);
            }
            dup2(p[1], STDOUT_FILENO); // Set output to the write-end of the new pipe
            close(p[0]);
            close(p[1]);

            vector<char*> args = parseCommand(parts[i]);
            if (args.empty() || args[0] == nullptr) _exit(1);
            execvp(args[0], args.data());
            perror("execvp failed for pipe segment");
            _exit(127);
        }

        close(p[1]); 
        if (in_fd != STDIN_FILENO) {
            close(in_fd); // Close the read-end of the previous pipe
        }
        in_fd = p[0]; 
    }

    
    if (in_fd != STDIN_FILENO) {
        dup2(in_fd, STDIN_FILENO); // Set input to the read-end of the last pipe
        close(in_fd);
    }

    vector<char*> args = parseCommand(parts.back());
    if (args.empty() || args[0] == nullptr) _exit(1);
    execvp(args[0], args.data());
    perror("execvp failed for last command in pipeline");
    _exit(127);
}

static pid_t launch_command(const string &cmd, const string &cwd, int &read_fd)
{
    if (cmd.find('|') != string::npos)
    {
        vector<string> parts;
        string tmp;
        stringstream ss(cmd);
        while (getline(ss, tmp, '|'))
        {
            parts.push_back(trim(tmp));
        }
        return launch_pipeline(parts, cwd, read_fd);
    }

    int out_pipe[2];
    if (pipe(out_pipe) < 0)
    {
        perror("launch_command: pipe failed");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("launch_command: fork failed");
        return -1;
    }

    if (pid == 0)
    {                  
        setpgid(0, 0); // Become a new process group leader
        if (chdir(cwd.c_str()) != 0)
        {
            _exit(127);
        }

        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);

        
        string command = cmd;
        string infile, outfile;
        bool appendMode = false;

        size_t outPos = command.find(">");
        if (outPos != string::npos)
        {
            if (outPos + 1 < command.size() && command[outPos + 1] == '>')
            {
                appendMode = true;
                outfile = command.substr(outPos + 2);
            }
            else
                outfile = command.substr(outPos + 1);
            command = command.substr(0, outPos);
            outfile.erase(0, outfile.find_first_not_of(" \t\n\r"));
            outfile.erase(outfile.find_last_not_of(" \t\n\r") + 1);
            int outfd = open(outfile.c_str(), O_WRONLY | O_CREAT | (appendMode ? O_APPEND : O_TRUNC), 0644);
            if (outfd < 0)
            {
                perror("open outfile");
                exit(1);
            }
            dup2(outfd, STDOUT_FILENO);
            dup2(outfd, STDERR_FILENO);
            close(outfd);
        }
        size_t inPos = command.find("<");
        if (inPos != string::npos)
        {
            infile = command.substr(inPos + 1);
            command = command.substr(0, inPos);
            infile.erase(0, infile.find_first_not_of(" \t\n\r"));
            infile.erase(infile.find_last_not_of(" \t\n\r") + 1);
            int infd = open(infile.c_str(), O_RDONLY);
            if (infd < 0)
            {
                perror("open infile");
                exit(1);
            }
            dup2(infd, STDIN_FILENO);
            close(infd);
        }

        vector<char *> args = parseCommand(command);
        execvp(args[0], args.data());
        perror("execvp failed");
        _exit(127);
    }

    close(out_pipe[1]);    
    read_fd = out_pipe[0]; // Return the read end to the caller.
    return pid;
}
static volatile sig_atomic_t multiWatch_stop = 0;
static vector<pid_t> mw_child_pids;
static vector<string> mw_temp_files;
void mw_sigint_handler(int signo)
{
    (void)signo;
    multiWatch_stop = 1;
}
void mw_sigcont_handler(int signo)
{
    (void)signo;
    // When the main multiWatch process is continued, continue its children too
    for (pid_t p : mw_child_pids)
    {
        if (p > 0)
        {
            kill(p, SIGCONT);
        }
    }
}
atomic<bool> multiWatchRunning(true);
vector<string> tempFiles;
static void runMultiWatch(vector<string> cmds, string currentDir, int out_fd)
{
    if (cmds.empty())
    {
        return;
    }

    // This function now writes its output directly to the parent via the pipe
    auto send_to_parent = [&](const string &line)
    {
        string msg = "OUT:" + line + "\n";
        write(out_fd, msg.c_str(), msg.length());
    };

    

    mw_child_pids.clear();
    mw_temp_files.clear();

    for (const string &c : cmds)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            continue;
        }

        if (pid == 0)
        {
            if (chdir(currentDir.c_str()) != 0)
            {
                perror("chdir in multiWatch child failed");
                _exit(1);
            }
            pid_t mypid = getpid();
            string fname = ".temp." + to_string(mypid) + ".txt";
            int outfd = open(fname.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (outfd < 0)
            {
                perror("open tempfile");
                _exit(1);
            }
            dup2(outfd, STDOUT_FILENO);
            dup2(outfd, STDERR_FILENO);
            close(outfd);
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0)
            {
                dup2(devnull, STDIN_FILENO);
                if (devnull > 2)
                    close(devnull);
            }
            
            int dummy_read_fd; // Not used, but the function requires it.
            execute_pipeline(c, currentDir);
            perror("execvp failed");
            _exit(1);
        }
        else
        {
            mw_child_pids.push_back(pid);
            string fname = ".temp." + to_string(pid) + ".txt";
            mw_temp_files.push_back(fname);
        }
    }

    size_t n = mw_temp_files.size();
    vector<int> fds(n, -1);
    vector<off_t> offsets(n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        int fd = -1, tries = 0;
        while (fd < 0 && tries++ < 10)
        {
            usleep(10000);
            fd = open(mw_temp_files[i].c_str(), O_RDONLY);
        }
        if (fd >= 0)
        {
            fds[i] = fd;
        }
    }

    const string sep(52, '-');
    char buf[4096];
    do
    {
        if (mw_controller_stop) {
            break; 
        }
        bool anyActiveChild = false;
        for (size_t i = 0; i < mw_child_pids.size(); ++i)
        {
            if (mw_child_pids[i] <= 0)
                continue;
            int status;
            pid_t r = waitpid(mw_child_pids[i], &status, WNOHANG);
            if (r == 0)
                anyActiveChild = true;
        }

        for (size_t i = 0; i < n; ++i)
        {
            if (fds[i] < 0)
                continue;

            // Seek to the last known offset, not the beginning
            lseek(fds[i], offsets[i], SEEK_SET);

            string accum;
            ssize_t r;
            // Read any new data from the current offset to the end
            while ((r = read(fds[i], buf, sizeof(buf))) > 0)
            {
                accum.append(buf, r);
            }

            if (!accum.empty())
            {
                offsets[i] = lseek(fds[i], 0, SEEK_CUR); // Update offset to current position
                time_t now = time(nullptr);
                string ts = ctime(&now);
                ts.pop_back();
                send_to_parent("\"" + cmds[i] + "\", " + ts + ":");
                send_to_parent(sep);

                stringstream lines(accum);
                string l;
                while (getline(lines, l))
                    send_to_parent(l);
                send_to_parent(sep);
            }
        }
        if (!anyActiveChild)
            break;
        this_thread::sleep_for(chrono::milliseconds(200));
    } while (!multiWatch_stop);

    
    for (pid_t p : mw_child_pids)
    {
        if (p > 0 && kill(p, 0) == 0) // Check if process exists
        {
            kill(p, SIGTERM); // terminate
        }
    }

    // a short delay to terminate
    usleep(50000); 

    for (pid_t p : mw_child_pids)
    {
        if (p > 0 && kill(p, 0) == 0) // Check if still alive
        {
            kill(p, SIGKILL); // force kill
        }
        if (p > 0) {
            waitpid(p, NULL, 0); // Wait for this specific child to be reaped
        }
    }

    for (int fd : fds)
        if (fd >= 0)
            close(fd);
    for (const string &f : mw_temp_files)
        unlink(f.c_str());

    signal(SIGINT, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
}

void tab_main(int in_fd, int out_fd, string initial_dir)
{
    prctl(PR_SET_PDEATHSIG, SIGHUP);

    string currentDir = initial_dir;
    string initial_cwd_msg = "CWD:" + currentDir + "\n";
    write(out_fd, initial_cwd_msg.c_str(), initial_cwd_msg.length());

    string command_buffer;
    int current_read_fd = -1; // FD for reading output from the running command
    string child_output_buffer;

    while (true)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(in_fd, &read_fds); // continuously monitor the input pipe

        int max_fd = in_fd;
        if (current_read_fd != -1)
        {
            FD_SET(current_read_fd, &read_fds);
            max_fd = max(max_fd, current_read_fd);
        }

        // Wait for activity on either pipe
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout
        select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        int status;
        while (waitpid(-1, &status, WNOHANG) > 0); // Reap any zombies

        if (FD_ISSET(in_fd, &read_fds))
        {
            char buffer[4096];
            ssize_t n = read(in_fd, buffer, sizeof(buffer));
            if (n <= 0)
                break; // Parent closed pipe, exit

            command_buffer.append(buffer, n);

            size_t pos;
            while ((pos = command_buffer.find('\1')) != string::npos)
            {
                string fullCommand = trim(command_buffer.substr(0, pos));
                command_buffer.erase(0, pos + 1);

               
                if (fullCommand == "__EXIT__")
                {
                    close(in_fd);
                    close(out_fd);
                    _exit(0);
                }
                if (fullCommand == "exit")
                {
                    saveHistory(fullCommand);
                    write(out_fd, "EXIT_PROG:\n", 11);
                }
                else if (fullCommand == "CLEAR_HISTORY_CHILD")
                {
                    
                    clearHistory();
                    write(out_fd, "CMD_DONE:\n", 10);
                }
                else if (fullCommand.rfind("cd ", 0) == 0)
                {
                    saveHistory(fullCommand);
                    string path = trim(fullCommand.substr(3));
                    if (chdir(path.c_str()) != 0)
                    {
                        string err_msg = "OUT:cd: " + path + ": " + strerror(errno) + "\n";
                        write(out_fd, err_msg.c_str(), err_msg.length());
                    }
                    else
                    {
                        char new_cwd[1024];
                        if (getcwd(new_cwd, sizeof(new_cwd)))
                        {
                            currentDir = new_cwd;
                            string cwd_msg = "CWD:" + currentDir + "\n";
                            write(out_fd, cwd_msg.c_str(), cwd_msg.length());
                        }
                    }
                    write(out_fd, "CMD_DONE:\n", 10);
                }
                else if (fullCommand == "clear")
                {
                    saveHistory(fullCommand);
                    write(out_fd, "CLEAR:\n", 7);
                    write(out_fd, "CMD_DONE:\n", 10);
                }
                else if (fullCommand == "__INTERRUPT__")
                {
                    
                    if (current_read_fd != -1) {
                        close(current_read_fd);
                        current_read_fd = -1;
                    }
                    write(out_fd, "CMD_DONE:\n", 10);
                }
                else if (fullCommand == "history")
                {
                    size_t start = commandHistory.size() > MAX_HISTORY_DISPLAY ? commandHistory.size() - MAX_HISTORY_DISPLAY : 0;
                    for (size_t i = start; i < commandHistory.size(); i++)
                    {
                        string history_line = "OUT:" + to_string(i + 1) + "  " + commandHistory[i] + "\n";
                        write(out_fd, history_line.c_str(), history_line.length());
                    }
                    write(out_fd, "CMD_DONE:\n", 10);
                }
                

                else if (fullCommand.rfind("multiWatch", 0) == 0)
                {
                    // Find the bounds of the command list
                    saveHistory(fullCommand);
                    size_t start = fullCommand.find('[');
                    size_t end = fullCommand.rfind(']'); 
                    
                    if (start != string::npos && end != string::npos && end > start)
                    {
                        string cmdsStr = fullCommand.substr(start + 1, end - start - 1);
                        vector<string> cmds;
                        
                        string current_cmd;
                        bool in_quotes = false;
                        
                        // quote-aware parser for the command list
                        for (size_t i = 0; i < cmdsStr.length(); ++i) {
                            char c = cmdsStr[i];

                            if (c == '\\') {
                                // Handle escape character
                                current_cmd.push_back(c); // Keep the backslash
                                if (i + 1 < cmdsStr.length()) {
                                    i++;
                                    current_cmd.push_back(cmdsStr[i]); // Keep the escaped char
                                }
                            } else if (c == '"') {
                                in_quotes = !in_quotes;
                                current_cmd.push_back(c); // Keep the quote
                            } else if (c == ',' && !in_quotes) {
                                // ',' found, push the command
                                string trimmed_cmd = trim(current_cmd);
                                if (!trimmed_cmd.empty()) {
                                    cmds.push_back(trimmed_cmd);
                                }
                                current_cmd.clear();
                            } else {
                                current_cmd.push_back(c);
                            }
                        }
                        
                        // Add the last command (if any)
                        string trimmed_cmd = trim(current_cmd);
                        if (!trimmed_cmd.empty()) {
                            cmds.push_back(trimmed_cmd);
                        }

                        
                        pid_t mw_pid = fork();

                        if (mw_pid < 0)
                        {
                            perror("fork for multiWatch failed");
                            write(out_fd, "CMD_DONE:\n", 10);
                        }
                        else if (mw_pid == 0)
                        {
                            // new grandchild process
                            prctl(PR_SET_PDEATHSIG, SIGHUP); // Dies if the tab process dies
                            setpgid(0, 0);                   // Become group leader
                            mw_controller_stop = 0;
                            struct sigaction act = {};
                            act.sa_handler = mw_controller_sigint_handler;
                            sigaction(SIGINT, &act, NULL);
                            while (!mw_controller_stop)
                            {
                                // Run the commands once and wait for them to finish
                                runMultiWatch(cmds, currentDir, out_fd);

                                if (mw_controller_stop) {
                                    break;
                                }

                                // Sleep for 2 seconds before the next run
                                struct timeval tv;
                                tv.tv_sec = 2;
                                tv.tv_usec = 0;
                                select(0, NULL, NULL, NULL, &tv);
                            }

                            // Signal completion and exit
                            write(out_fd, "CMD_DONE:\n", 10);
                            _exit(0);
                        }
                        else
                        {
                            
                            string pgid_msg = "PGID:" + to_string(mw_pid) + "\n";
                            write(out_fd, pgid_msg.c_str(), pgid_msg.length());
                            // The tab now simply waits for the multiWatch process to send CMD_DONE
                        }
                    }
                    else
                    {
                        // Handle the case where the command syntax is wrong.
                        const char *err_msg = "OUT:multiWatch: incorrect syntax. Use: multiWatch [cmd1,cmd2,...]\n";
                        write(out_fd, err_msg, strlen(err_msg));
                        // Send CMD_DONE since there's nothing to run.
                        write(out_fd, "CMD_DONE:\n", 10);
                    }
                }
                else
                { // External command
                    saveHistory(fullCommand);
                    pid_t cmd_pgid = launch_command(fullCommand, currentDir, current_read_fd);

                    if (cmd_pgid > 0)
                    {
                        string pgid_msg = "PGID:" + to_string(cmd_pgid) + "\n";
                        write(out_fd, pgid_msg.c_str(), pgid_msg.length());
                    }
                    else
                    {
                        // Command failed to launch
                        write(out_fd, "CMD_DONE:\n", 10);
                        current_read_fd = -1;
                    }
                }
            }
        }

        // Output from the currently running command
        if (current_read_fd != -1 && FD_ISSET(current_read_fd, &read_fds))
        {
            char read_buffer[4096];
            ssize_t bytes_read = read(current_read_fd, read_buffer, sizeof(read_buffer));

            if (bytes_read > 0)
            { // append it to stream buffer
                child_output_buffer.append(read_buffer, bytes_read);

                size_t pos;
                while ((pos = child_output_buffer.find('\n')) != string::npos) {
                    string line = child_output_buffer.substr(0, pos);
                    string out_msg = "OUT:" + line + "\n";
                    write(out_fd, out_msg.c_str(), out_msg.length());
                    
                    child_output_buffer.erase(0, pos + 1);
                }
            }
            else
            { 
                if (!child_output_buffer.empty()) {
                    string out_msg = "OUT:" + child_output_buffer + "\n";
                    write(out_fd, out_msg.c_str(), out_msg.length());
                    child_output_buffer.clear();
                }
                
                close(current_read_fd);
                current_read_fd = -1;
                write(out_fd, "CMD_DONE:\n", 10);
            }
        }
    }
    _exit(0);
}