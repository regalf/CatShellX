/*
 * ptytest: run Build/catshellx under a real PTY and feed it a scripted
 * keystroke sequence, capturing the output.  Script grammar:
 *   # comment
 *   @text <str>        type literal bytes (no Enter)
 *   @enter             press Return
 *   @ctrl <A-Za-z>     press Control-letter (0x01..0x1a)
 *   @tab  @backspace   press Tab / Backspace (0x7f)
 *   @up @down @right @left @home @end @del @altleft @altright
 *   @sleep <ms>        pause (and drain output)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <util.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>

static FILE *g_log = NULL;

static void drain(int master, int ms)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(master, &r);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = ms * 1000;
    while (select(master + 1, &r, NULL, NULL, &tv) > 0) {
        char buf[8192];
        int n = read(master, buf, sizeof(buf));
        if (n <= 0)
            break;
        fwrite(buf, 1, n, stdout);
        fflush(stdout);
        if (g_log) {
            fwrite(buf, 1, n, g_log);
            fflush(g_log);
        }
        FD_ZERO(&r);
        FD_SET(master, &r);
        tv.tv_sec = 0;
        tv.tv_usec = ms * 1000;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: ptytest <script>\n");
        return 2;
    }
    FILE *sf = fopen(argv[1], "r");
    if (!sf) {
        perror(argv[1]);
        return 2;
    }

    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) != 0) {
        perror("openpty");
        return 2;
    }
    g_log = fopen("/tmp/ptytest_out.txt", "w");
    if (!g_log)
        g_log = stderr;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        close(master);
        setenv("HOME", "/tmp/csx_home", 1);
        execlp("./Build/catshellx", "catshellx", NULL);
        perror("exec");
        _exit(127);
    }
    close(slave);

    struct termios t;
    tcgetattr(master, &t);
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(master, TCSANOW, &t);

    char line[4096];
    while (fgets(line, sizeof(line), sf)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (!n || line[0] == '#')
            continue;
        char c;
        const char *s;
        int len = 0;
        if (strncmp(line, "@text ", 6) == 0) {
            s = line + 6;
            len = (int)strlen(s);
        } else if (strncmp(line, "@ctrl ", 6) == 0 && strlen(line) >= 7 && line[6]) {
            c = (line[6] >= 'a' ? line[6] - 'a' : line[6] - 'A') + 1;
            s = &c;
            len = 1;
        } else if (strcmp(line, "@enter") == 0) {
            c = '\r';
            s = &c;
            len = 1;
        } else if (strcmp(line, "@tab") == 0) {
            c = 0x09;
            s = &c;
            len = 1;
        } else if (strcmp(line, "@backspace") == 0) {
            c = 0x7f;
            s = &c;
            len = 1;
        } else if (strcmp(line, "@up") == 0) {
            s = "\x1b[A";
            len = 3;
        } else if (strcmp(line, "@down") == 0) {
            s = "\x1b[B";
            len = 3;
        } else if (strcmp(line, "@right") == 0) {
            s = "\x1b[C";
            len = 3;
        } else if (strcmp(line, "@left") == 0) {
            s = "\x1b[D";
            len = 3;
        } else if (strcmp(line, "@home") == 0) {
            s = "\x1b[H";
            len = 3;
        } else if (strcmp(line, "@end") == 0) {
            s = "\x1b[F";
            len = 3;
        } else if (strcmp(line, "@del") == 0) {
            s = "\x1b[3~";
            len = 4;
        } else if (strcmp(line, "@altright") == 0) {
            s = "\x1b" "f";
            len = 2;
        } else if (strcmp(line, "@altleft") == 0) {
            s = "\x1b" "b";
            len = 2;
        } else if (strcmp(line, "@altd") == 0) {
            s = "\x1b" "d";
            len = 2;
        } else if (strcmp(line, "@alty") == 0) {
            s = "\x1b" "y";
            len = 2;
        } else if (strcmp(line, "@ctrl_") == 0) {
            c = 0x1f;
            s = &c;
            len = 1;
        } else if (strncmp(line, "@sleep ", 7) == 0) {
            usleep(atoi(line + 7) * 1000);
            drain(master, 20);
            continue;
        } else {
            continue;
        }
        if (len > 0) {
            fprintf(stderr, "[send %s] ", line);
            {
                int i;
                for (i = 0; i < len; i++)
                    fprintf(stderr, "0x%02x ", (unsigned char)s[i]);
                fprintf(stderr, "\n");
            }
            write(master, s, (size_t)len);
        }
        usleep(30000);
        drain(master, 10);
    }

    int status;
    int i;
    for (i = 0; i < 200; i++) {
        drain(master, 20);
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        usleep(100000);
    }
    if (i >= 200) {
        fprintf(stderr, "\n[ptytest] WATCHDOG: child did not exit, killing it\n");
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    drain(master, 100);
    close(master);
    fclose(sf);
    if (g_log && g_log != stderr)
        fclose(g_log);
    return 0;
}
