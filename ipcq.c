/*
 * ipcq.c
 * command to do IPC using message queues
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MSIZE           1024
#define PROJ_ID         'A'

#define ACTION_UNKNOWN  0
#define ACTION_QUEUE    1
#define ACTION_TIME     2
#define ACTION_GET      3
#define ACTION_UNIQ_Q   4
#define ACTION_Q_EXISTS 5

/*
 * Tune sql_get_t polling frequency
 */
#define POLLS_PER_SECOND 3
#define POLL_INTERVAL_USEC (1000000 / POLLS_PER_SECOND)


struct msgbuf {
    long mtype;
    char mtext[MSIZE];
};


static void help(char *, int);
static int cleanq(int);
static key_t getk(char *);
static int getqk(char *);
static int getqk_exists(char *);
static int getqk_uniq(char *);
static int sq_queue(char *, char *);
static int sq_get(char *, int);
static int sq_get_t(char *, int, int);

int main(int argc, char *argv[])
{
    int i;
    int action = ACTION_UNKNOWN;
    int wtime = 0;
    int clean = 0;
    char *path = NULL;
    char *message = NULL;

    for (i = 1; i < argc && argv[i][0] == '-'; ++i) {
        switch (argv[i][1]) {
            case 'c':
            case 'C':
                clean = 1;
                break;
            case 'e':
            case 'E':
                action = ACTION_Q_EXISTS;
                break;
            case 'f':
            case 'F':
                if (argv[i][2]) {
                    path = &argv[i][2];
                } else if (i + 1 < argc) {
                    i++;
                    path = argv[i];
                } else {
                    help(argv[0], EXIT_FAILURE);
                }
                break;
            case 'g':
            case 'G':
                action = ACTION_GET;
                break;
            case 'h':
            case 'H':
            case '?':
                help(argv[0], EXIT_SUCCESS);
                /* break not required - help exits */
            case 'n':
            case 'N':
                action = ACTION_UNIQ_Q;
                break;
            case 'q':
            case 'Q':
                action = ACTION_QUEUE;
                if (argv[i][2]) {
                    message = &argv[i][2];
                } else if (i + 1 < argc) {
                    i++;
                    message = argv[i];
                } else {
                    help(argv[0], EXIT_FAILURE);
                }
                break;
            case 't':
            case 'T':
                action = ACTION_TIME;
                if (argv[i][2]) {
                    wtime = atoi(&argv[i][2]);
                } else if (i + 1 < argc) {
                    i++;
                    wtime = atoi(argv[i]);
                } else {
                    help(argv[0], EXIT_FAILURE);
                }
                break;
            default:
                help(argv[0], EXIT_FAILURE);
        }
    }
    if (!path && i < argc) {
        path = argv[i];
    }
    if (!path) {
        help(argv[0], EXIT_FAILURE);
    }


    switch (action) {
        case ACTION_GET:
            return sq_get(path, clean);
        case ACTION_QUEUE:
            return sq_queue(path, message);
        case ACTION_Q_EXISTS:
            if (getqk_exists(path) < 0)
                return EXIT_FAILURE;
            return EXIT_SUCCESS;
        case ACTION_TIME:
            if (sq_get_t(path, clean, wtime) < 0)
                return EXIT_FAILURE;
            return EXIT_SUCCESS;
        case ACTION_UNIQ_Q:
            if (getqk_uniq(path) < 0)
                return EXIT_FAILURE;
            return EXIT_SUCCESS;
        default:
            if (clean)
                if (cleanq(getqk(path)) < 0)
                    return EXIT_FAILURE;
                return EXIT_SUCCESS;
            help(argv[0], EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}


/*
 * Exit the program with a message on use.
 * char *filename[] == the address of argv[0] in main
 * exit_status == set the error level at exit and decide between stdin or
 *                stdout; EXIT_SUCCESS or EXIT_FAILURE should be used
 */
void help(char *filename, int exit_status)
{
    FILE *output;

    if (exit_status == EXIT_SUCCESS)
        output = stdout;
    else
        output = stderr;

    fprintf(output, "Communicate with Message Queues\n");
    fprintf(output, "\n");
    fprintf(output, "Usage:\n");
    fprintf(output, "  %s -q <message> -f <file>\n", filename);
    fprintf(output, "  %s -g [-c] -f <file>\n", filename);
    fprintf(output, "  %s -t <seconds> [-c] -f <file>\n", filename);
    fprintf(output, "  %s -e -f <file>\n", filename);
    fprintf(output, "  %s -n -f <file>\n", filename);
    fprintf(output, "  %s -c -f <file>\n", filename);
    fprintf(output, "  %s -h\n", filename);
    fprintf(output, "\n");
    fprintf(output, "Options:\n");
    fprintf(output, "  -q  Queue a <message>.\n");
    fprintf(output, "  -f  The queue <file> that defines the queue.\n");
    fprintf(output, "  -g  Get a message from the queue.\n");
    fprintf(output, "      Block until available.\n");
    fprintf(output, "  -t  Get a message from the queue.\n");
    fprintf(output, "      Give up in defined <seconds>.\n");
    fprintf(output, "  -e  Return success if a queue already exists.\n");
    fprintf(output, "  -n  Create a new queue if one does not exist.\n");
    fprintf(output, "  -c  Clean queue.\n");
    fprintf(output, "  -h  Display this help message.\n");

    exit(exit_status);
}


/*
 * Put a message on the queue
 *
 * char * path is the project path
 */
int sq_queue(char *path, char *message)
{
    struct msgbuf buf;
    int msqk;

    buf.mtype = 1;

    if ((msqk = getqk(path)) < 0) {
        return -1;
    }

    if (message) {
        /* Copy message into buffer, ensuring null termination */
        strncpy(buf.mtext, message, MSIZE - 1);
        buf.mtext[MSIZE - 1] = '\0';  /* guarantee null-terminated string */
    } else {
        buf.mtext[0] = '\0';  /* send empty string if message is NULL */
    }

    /* Send the string including the null terminator */
    size_t msg_len = strlen(buf.mtext) + 1;

    return msgsnd(msqk, &buf, msg_len, 0);
}


/*
 * Get a message from the queue and block until available
 *
 * char * path is the string representing the project path
 */
int sq_get(char *path, int clean)
{
    struct msgbuf buf;
    int msqk;

    if ((msqk = getqk(path)) < 0) {
        return -1;
    }

    buf.mtext[0] = '\0';
    if (msgrcv(msqk, &buf, MSIZE, 0, 0) < 0) {
        return -1;
    }

    if (buf.mtext[0]) {
        printf("%s\n", buf.mtext);
    }

    if (clean) {
        if (cleanq(msqk) == -1) {
            return -1;
        }
    }

    return 0;
}


/*
 * Get a message from the queue and block until available
 * or ~ sec seconds have passed.
 *
 * char * path is the string representing the project path
 */
int sq_get_t(char *path, int clean, int sec)
{
    struct msgbuf buf;
    int msqk;
    int retval = -1;
    int i;
    int total_attempts;

    if ((msqk = getqk(path)) < 0) {
        return -1;
    }

    buf.mtext[0] = '\0';
    total_attempts = sec * POLLS_PER_SECOND;
    for (i = 0; i < total_attempts; i++) {
        if (msgrcv(msqk, &buf, MSIZE, 0, IPC_NOWAIT) >= 0) {
            retval = 0;
            break;
        }

        if (errno != ENOMSG) {
            return -1;
        }

        usleep(POLL_INTERVAL_USEC);
    }
    if (buf.mtext[0]) {
        printf("%s\n", buf.mtext);
    }

    if (clean) {
        if (cleanq(msqk) == -1) {
            return -1;
        }
    }

    return retval;
}

/*
 * Get a key for the project path
 */
key_t getk(char *path)
{
    return ftok(path, PROJ_ID);
}


/*
 * Get the message queue for the path
 */
int getqk(char *path)
{
    return msgget(getk(path), 0666 | IPC_CREAT);
}


/*
 * Create a new message queue for the path
 * return < 0 if it already exists.
 */
int getqk_uniq(char *path)
{
    return msgget(getk(path), 0666 | IPC_CREAT | IPC_EXCL);
}

/*
 * Get an existing message queue for the path
 * return < 0 if it cannot be obtained.
 */
int getqk_exists(char *path)
{
    return msgget(getk(path), 0);
}


/*
 * Cleanup the queue for the project path
 */
int cleanq(int q)
{
    return msgctl(q, IPC_RMID, NULL);
}
