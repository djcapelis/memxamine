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
#include<strings.h>
#include<dirent.h>
#include<sys/stat.h>

/* Function prototypes */
void print_usage();
void err_msg(char * msg);
void err_exit();
int filter(const struct dirent * f);
int cmp(const struct dirent ** p0, const struct dirent ** p1);

/* Defines */
#define NAMELEN 256       /* Length of max file name */

/* Macros */
#define err_chk(cond) do { if(cond) { goto err; }} while(0)

/* Globals */
bool OPT_B = false;     /* -b specified */
bool OPT_K = false;     /* -k specified */
bool OPT_P = false;     /* -p specified */
bool OPT_U = false;     /* -u specified */
bool OPT_Q = false;     /* -q specified */
int pid;                /* pid of process differences to examine */
int maxseg;             /* maximum segment number */
int maxsnap;            /* maximum snapshot number */

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
    int blocksize;          /* assume a blocksize of this size */
    int chk;                /* return value check */
    char * srcdir;          /* where the memdiffs are */

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
        srcdir = calloc(1, 2);
        srcdir[0] = '.';
        srcdir[1] = '\0';
    }
    else
    {
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

    struct dirent ** flist;
    int files;
    files = scandir(srcdir, &flist, &filter, &cmp);
    for(int i = 0; i < files;i++)
        printf("%s\n", flist[i]->d_name);
    printf("maxseg: %d, maxsnap %d, total files %d\n", maxseg, maxsnap, files);
    err_chk(files == -1);

    exit(EXIT_SUCCESS);

err:
    if(errno)
        perror("memxamine");
    if(srcdir)
        free(srcdir);
    exit(EXIT_FAILURE);
}

/* Filter function for scandir() */
int filter(const struct dirent * f)
{
    char chkchars[64];
    char * tok;
    int i;

    if(f->d_type != DT_REG)
        return 0;

    /* Check for .memdiff extension */
    if(rindex(f->d_name, '.') == NULL)
        return 0;
    if(strcmp(".memdiff", rindex(f->d_name, '.')) != 0)
        return 0;

    /* Check for correct pid name format */
    snprintf(chkchars, 64, "pid%d_snap", pid);
    if(strncmp(f->d_name, chkchars, strlen(chkchars)) != 0)
        return 0;

    /* Check for correct snap format */
    tok = index(f->d_name, '_');
    if(tok == NULL)
        return 0;
    tok++;
    tok = index(tok, '_');
    if(tok == NULL)
        return 0;
    if(strncmp("_snap", tok, 5) != 0)
        return 0;
    tok += 5;
    sscanf(tok, "%d", &i);
    if(maxsnap < i)
        maxsnap = i;

    /* Check for correct segment format */
    tok = index(tok, '_');
    if(tok == NULL)
        return 0;
    if(strncmp("_seg", tok, 4) != 0)
        return 0;
    tok += 4;
    sscanf(tok, "%d", &i);
    if(maxseg < i)
        maxseg = i;

    return 1;
}

/* Comparison function for scandir() */
int cmp(const struct dirent ** p0, const struct dirent ** p1)
{
    const struct dirent * f0 = *p0;
    const struct dirent * f1 = *p1;
    int seg0;
    int seg1;
    int snap0;
    int snap1;
    char * tok;

    /* Find segments */
    /* First segment */
    tok = rindex(f0->d_name, '_');
    if(tok == NULL)
        return 0; // shouldn't happen
    tok += 4;
    sscanf(tok, "%d", &seg0);

    /* Second segment */
    tok = rindex(f1->d_name, '_');
    if(tok == NULL)
        return 0; // shouldn't happen
    tok += 4;
    sscanf(tok, "%d", &seg1);

    /* Segment comparison */
    if(seg0 != seg1)
        return seg0 > seg1 ? 1 : -1;

    /* Find snaps */
    /* First snap */
    tok = index(f0->d_name, '_');
    if(tok == NULL)
        return 0; //shouldn't happen
    tok++;
    tok = index(tok, '_');
    if(tok == NULL)
        return 0; //shouldn't happen
    tok += 5;
    sscanf(tok, "%d", &snap0);

    /* Second snap */
    tok = index(f1->d_name, '_');
    if(tok == NULL)
        return 0; //shouldn't happen
    tok++;
    tok = index(tok, '_');
    if(tok == NULL)
        return 0; //shouldn't happen
    tok += 5;
    sscanf(tok, "%d", &snap1);

    /* Snap comparison */
    if(snap0 != snap1)
        return snap0 > snap1 ? 1 : -1;

    if(seg0 == seg1 && snap0 == snap1)
    {
        fprintf(stderr, "Warning: cannot differentiate between %s and %s\n", f0->d_name, f1->d_name);
    }

    return 0;
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
