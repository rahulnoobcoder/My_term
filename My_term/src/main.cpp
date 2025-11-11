#include "app_logic.hpp"
#include "x11.hpp"
#include "history.hpp"
#include <iostream>
#include <chrono>
#include <sys/select.h>
#include <csignal>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xlocale.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <X11/Xatom.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <locale.h>
#include <thread>
#include <bits/stdc++.h>
#include <glob.h>
#include <atomic>
#include <dirent.h>
#include <algorithm>
#include <sys/prctl.h>
using namespace std;
int main()

{
    setlocale(LC_ALL, "");
    loadHistory();

    X11Context ctx;
    if (!setup_x11(ctx))
    {
        cerr << "Failed to set up X11 context." << endl;
        return 1;
    }

    char initial_cwd_buf[1024];
    if (getcwd(initial_cwd_buf, sizeof(initial_cwd_buf)))
    {
        createNewTab(initial_cwd_buf);
    }
    else
    {
        createNewTab("/"); // Fallback
    }

    bool cursorVisible = true;
    auto lastBlink = chrono::steady_clock::now();
    int x11_fd = ConnectionNumber(ctx.display);
    string message_buffer;

    auto last_job_check = chrono::steady_clock::now();
    // Force a draw of the very first frame with the cursor visible.
    draw_frame(ctx, true);
    XFlush(ctx.display);

    lastBlink = chrono::steady_clock::now();

    while (true)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(x11_fd, &read_fds);
        int max_fd = x11_fd;

        if (activeTab != -1 && tabs[activeTab].child_to_parent_fd[0] != -1)
        {
            int child_fd = tabs[activeTab].child_to_parent_fd[0];
            FD_SET(child_fd, &read_fds);
            max_fd = max(max_fd, child_fd);
        }

        struct timeval timeout = {0, 100000}; // 100ms
        select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activeTab != -1 && tabs[activeTab].child_to_parent_fd[0] != -1 && FD_ISSET(tabs[activeTab].child_to_parent_fd[0], &read_fds))
        {
            char buffer[4096];
            ssize_t n = read(tabs[activeTab].child_to_parent_fd[0], buffer, sizeof(buffer) - 1);
            if (n > 0)
            {
                buffer[n] = '\0';
                message_buffer += buffer;
                size_t pos;
                while ((pos = message_buffer.find('\n')) != string::npos)
                {
                    handle_child_message(message_buffer.substr(0, pos));
                    message_buffer.erase(0, pos + 1);
                }
            }
        }

        while (XPending(ctx.display))
        {
            XEvent e;
            XNextEvent(ctx.display, &e);
            if (!handle_x11_event(e, ctx))
            {
                goto exit_main_loop;
            }
        }

        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - last_job_check).count() >= 1)
        {
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                auto &tab = tabs[i];
                for (int j = tab.jobs.size() - 1; j >= 0; --j)
                {
                    auto &job = tab.jobs[j];
                    // kill with signal 0 checks for process existence without sending a signal.
                    if (job.status == "Running" && kill(-(job.pgid), 0) == -1 && errno == ESRCH)
                    {
                        job.status = "Done";

                        if ((int)i == activeTab)
                        {
                            tab.outputLines.push_back(
                                "Done            [" + to_string(job.id) + "]+ " + job.command);
                        }
                    }
                }
            }
            last_job_check = now;
        }
        if (chrono::duration_cast<chrono::milliseconds>(now - lastBlink).count() > 500)
        {
            cursorVisible = !cursorVisible;
            lastBlink = now;
        }

        draw_frame(ctx, cursorVisible);
        XFlush(ctx.display);
    }

exit_main_loop:
    for (auto &tab : tabs)
    {
        if (tab.pid > 0)
        {
            kill(tab.pid, SIGTERM);
            waitpid(tab.pid, NULL, 0);
        }
    }
    cleanup_x11(ctx);
    return 0;
}