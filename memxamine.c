/**********************************************************************
 * memxamine.c                                                        *
 *                                                                    *
 * The logic of memxamine, all in a jumble.                           *
 *                                                                    *
 *********************************************************************/

#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<stdbool.h>
#include<limits.h>
#include<string.h>
#include<ftw.h>

/* Function prototypes */
void print_usage();
void err_msg(char * msg);
void err_exit();

/* Defines */
#define NAMELEN 256       /* Length of max file name */

/* Macros */
#define err_chk(cond) do { if(cond) { goto err; }} while(0)

/* Globals */
bool OPT_B = false;
bool OPT_K = false;
bool OPT_P = false;
bool OPT_U = false;
bool OPT_Q = false;

/* Print tin label */
void print_usage()
{
    fprintf(stderr, "Usage: memxamine <options> -p <pid> [memdiffs path]\n");
    fprintf(stderr, "\t-h Print usage\n");
    fprintf(stderr, "\t-b <size> assume blocksize of <size> bytes\n");
    fprintf(stderr, "\t-k <size> assume blocksize of <size> kilobytes\n");
    fprintf(stderr, "\t-p <pid> Look for memdiff files from pid <pid>\n");
    fprintf(stderr, "\t-u list unchanged segments\n");
    fprintf(stderr, "\t-q quieter output: squelch unnecessary messages\n");
}

int main(int argc, char * argv[])
{
    /* Variables! */
    int pid;                /* pid of process differences to examine */
    int blocksize;          /* assume a blocksize of this size */
    int chk;                /* return value check */
    char * srcdir;          /* where the memdiffs are */
    size_t srcdirlen;       /* length of diff directory */

    /* Argument parsing */
    char opt;               /* Char for getopt() */
    char * strerr = NULL;   /* Per getopt(3) */
    long arg;               /* Temporary argument storage */
    struct stat statchk;    /* For checking path validity */
    while((opt = getopt(argc, argv, "+hb:k:p:uq")) != -1)
    {
        switch(opt)
        {
            /* Specify pid */
            case 'p':
                if(OPT_P)
                    err_msg("memxamine does not currently support multiple -p arguments\n");
                OPT_P = true;
                arg = strtol(optarg, &strerr, 10);
                if(arg > INT_MAX || arg < 0 || strerr[0] != 0)
                    err_msg("Unable to parse -p argument correctly, should be a pid to look for memdiff files from\n");
                pid = arg;
                optarg = NULL;
                break;
            /* Blocksize in bytes */
            case 'b':
                if(OPT_B || OPT_K)
                    err_msg("Only one -b or -k option should be necessary\n");
                OPT_B = true;
                arg = strtol(optarg, &strerr, 10);
                if(arg > INT_MAX || arg < 0 || strerr[0] != 0)
                    err_msg("Unable to parse -b argument correctly, should be number of bytes\n");
                if((((arg - 1) & arg) != 0) || arg <= 0) /* If blocksize is not a positive, non-zero power of two */
                    err_msg("Unable to parse -b argument correctly.  Must be power of two.\n");
                blocksize = arg;
                optarg = NULL;
                break;
            /* Blocksize in kilobytes */
            case 'k':
                if(OPT_B || OPT_K)
                    err_msg("Only one -b or -k option should be necessary\n");
                OPT_K = true;
                arg = strtol(optarg, &strerr, 10);
                if((arg * 1024) > INT_MAX || arg < 0 || strerr[0] != 0)
                    err_msg("Unable to parse -k argument correctly, should be number of kilobytes\n");
                if((((arg - 1) & arg) != 0) || (arg * 1024) <= 0) /* If blocksize is not a positive, non-zero power of two */
                    err_msg("Unable to parse -k argument correctly.  Must be power of two.\n");
                blocksize = arg * 1024;
                optarg = NULL;
                break;
            /* Show unchanged segments */
            case 'u':
                OPT_U = true;
                break;
            /* Squelch unnecessary output */
            case 'q':
                OPT_Q = true;
                break;
            /* Print usage */
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
            default:
                print_usage();
                exit(EXIT_FAILURE);
        }
    }
    /* Option validity checks */
    if(!OPT_P)
        err_msg("Option -p must be specified");
    if(argc <= optind)
    {
        if(!OPT_Q)
            printf("No path given, searching current directory.\n");
        srcdirlen = 2;
        srcdir = calloc(1, 2);
        srcdir[0] = '.';
        srcdir[1] = '\0';
    }
    else
    {
        srcdirlen = strlen(argv[optind]);
        srcdir = argv[optind];
        chk = stat(srcdir, &statchk);
        if(chk == -1 && errno == ENOENT)
        {
            fprintf(stderr, "Invalid memdiff path: %s\n", srcdir);
            err_exit();
        }
        else if(chk == -1)
        {
            perror("Error parsing path to memdiffs:"); /* Undefined error, handle using perror() */
            exit(EXIT_FAILURE);
        }
        if(!S_ISDIR(statchk.st_mode))
        {
            fprintf(stderr, "%s is not a directory\n", srcdir);
            err_exit();
        }
    }

    exit(EXIT_SUCCESS);

err:
    if(errno)
        perror("memxamine");
    if(srcdir)
        free(srcdir);
    exit(EXIT_FAILURE);
}

/* Print error message, exit with errors */
void err_msg(char * msg)
{
    fprintf(stderr, "%s\n", msg);
    err_exit();
}

void err_exit()
{
    print_usage();
    exit(EXIT_FAILURE);
}
