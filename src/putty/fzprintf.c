#include "putty.h"
#include "misc.h"

bool pending_reply = false;

int fznotify(sftpEventTypes type)
{
    if (type == sftpDone || type == sftpReply) {
        pending_reply = false;
    }
    fprintf(stdout, "%c", (int)type + '0');
    fflush(stdout);
    return 0;
}

int fzprintf(sftpEventTypes type, const char* fmt, ...)
{
    if (type == sftpDone || type == sftpReply) {
        pending_reply = false;
    }
        
    va_list ap;
    char* str, *p, *s;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    if (!*str) {
        sfree(str);
        va_end(ap);

        fprintf(stdout, "%c\n", (int)type + '0');
        fflush(stdout);

        return 0;
    }
    p = str;
    s = str;
    while (1) {
        if (*p == '\r' || *p == '\n') {
            if (p != s) {
                *p = 0;
                fprintf(stdout, "%c%s\n", (int)type + '0', s);
                s = p + 1;
            }
            else {
                s++;
            }
        }
        else if (!*p) {
            if (p != s) {
                *p = 0;
                fprintf(stdout, "%c%s\n", (int)type + '0', s);
                s = p + 1;
            }
            break;
        }
        p++;
    }
    fflush(stdout);

    sfree(str);

    va_end(ap);

    return 0;
}

int fzprintf_raw_untrusted(sftpEventTypes type, const char* fmt, ...)
{
    if (type == sftpDone || type == sftpReply) {
        pending_reply = false;
    }

    va_list ap;
    char* str, *p, *s;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    p = str;
    s = str;
    while (*p) {
        if (*p == '\r') {
            p++;
        }
        else if (*p == '\n') {
            if (s != str) {
                *s++ = ' ';
            }
            p++;
        }
        else if (*p) {
            *s++ = *p++;
        }
    }
    *s = 0;

    if (type != sftpUnknown) {
        fputc((int)type + '0', stdout);
    }
    fputs(str, stdout);
    fputc('\n', stdout);
    fflush(stdout);

    sfree(str);

    va_end(ap);

    return 0;
}

int fzprintf_raw(sftpEventTypes type, const char* fmt, ...)
{
    if (type == sftpDone || type == sftpReply) {
        pending_reply = false;
    }

    va_list ap;
    char* str ;
    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);

    fputc((char)type + '0', stdout);
    fputs(str, stdout);
    fflush(stdout);

    sfree(str);

    va_end(ap);

    return 0;
}

int fznotify1(sftpEventTypes type, int data)
{
    if (type == sftpDone || type == sftpReply) {
        pending_reply = false;
    }

    fprintf(stdout, "%c%d\n", (int)type + '0', data);
    fflush(stdout);
    return 0;
}

