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
#include<fcntl.h>
#include<sys/mman.h>

/* Function prototypes */
void print_usage();
void err_msg(char * msg);
void err_exit();
unsigned long long popcounter(unsigned char * start, unsigned long long len);
int filter(const struct dirent * f);
int cmp(const struct dirent ** p0, const struct dirent ** p1);

/* Defines */
#define NAMELEN 256       /* Length of max file name */

/* Macros */
#define err_chk(cond) do { if(cond) { goto err; }} while(0)

/* Structures */
struct segdata
{
    unsigned long long size;
    unsigned long long ones;
};

/* Globals */
bool OPT_B = false;         /* -b specified */
bool OPT_K = false;         /* -k specified */
bool OPT_L = false;         /* -l specified */
bool OPT_P = false;         /* -p specified */
bool OPT_U = false;         /* -u specified */
bool OPT_Q = false;         /* -q specified */
int pid;                    /* pid of process differences to examine */
int maxseg;                 /* maximum segment number */
int maxsnap;                /* maximum snapshot number */
struct segdata ** data;     /* data array */
bool success = false;       /* successful exit? */

/* Print tin label */
void print_usage()
{
    fprintf(stderr, "Usage: memxamine <options> -p <pid> [memdiffs path]\n");
    fprintf(stderr, "\t-h Print usage\n");
    /*fprintf(stderr, "\t-b <size> assume blocksize of <size> bytes\n");
    fprintf(stderr, "\t-k <size> assume blocksize of <size> kilobytes\n");*/
    fprintf(stderr, "\t-l <limit> ignore segments with under <limit> blocks\n");
    fprintf(stderr, "\t-p <pid> Look for memdiff files from pid <pid>\n");
    fprintf(stderr, "\t-u list unchanged segments\n");
    fprintf(stderr, "\t-q quieter output: squelch unnecessary messages\n");
}

int main(int argc, char * argv[])
{
    /* Variables! */
    int blocksize;                  /* assume a blocksize of this size */
    int chk;                        /* return value check */
    char * srcdir;                  /* where the memdiffs are */
    char * fpath;                   /* file paths */
    int i;                          /* counting variable */
    struct segdata * base = NULL;   /* underlying memory for the data array */
    char fname[NAMELEN];            /* file name string */
    int * snapcols;                 /* column output alignment */
    unsigned int limit;             /* size limit */

    /* Initialization */
    base = NULL;
    data = NULL;
    fpath = NULL;
    snapcols = NULL;

    /* Argument parsing */
    char opt;               /* Char for getopt() */
    char * strerr = NULL;   /* Per getopt(3) */
    long arg;               /* Temporary argument storage */
    struct stat statchk;    /* For checking path validity */
    while((opt = getopt(argc, argv, "+hb:k:l:p:uq")) != -1)
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
            case 'l':
                if(OPT_L)
                    err_msg("Duplicate -l arguments\n");
                OPT_L = true;
                arg = strtol(optarg, &strerr, 10);
                if(arg > INT_MAX || arg < 0 || strerr[0] != 0)
                    err_msg("Unable to parse -l argument correctly, invalid number\n");
                limit = arg;
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
        err_msg("Option -p must be specified\n");
    if(OPT_U && OPT_L)
        err_msg("Options -l and -u conflict\n");
    if(argc <= optind)
    {
        if(!OPT_Q)
            printf("No path given, searching current directory.\n");
        srcdir = calloc(1, 2); /* This allocation leaks.  It will only ever leak two bytes. */
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
    fpath = calloc(strlen(srcdir) + NAMELEN, sizeof(char));
    err_chk(fpath == NULL);

    /* File picking */
    struct dirent ** flist;
    int files;
    files = scandir(srcdir, &flist, &filter, &cmp);
/*    for(int i = 0; i < files;i++)
        printf("%s\n", flist[i]->d_name);*/
    maxseg++; // segments are numbered from zero, so the max seg is actually one higher
    maxsnap--; // snapshots on the other hand, are numbered from one, we track only diffs, not inclusive
    if(!OPT_Q)
        printf("Reading %d files for pid %d over %d snapshots on memory regions 0 through %d\n\n", files, pid, maxsnap+1, maxseg);
    err_chk(files == -1);

    /* Data array initialization */
    base = calloc(maxseg * maxsnap, sizeof(struct segdata));
    err_chk(base == NULL);
    data = calloc(maxseg, sizeof(struct segdata *));
    err_chk(data == NULL);
    for(i = 0; i < maxseg; ++i)
        data[i] = base + (i * maxsnap);

    /* Determine segment changes */
    int curfile = 0;
    strcpy(fpath, srcdir);
    if(fpath[strlen(fpath)-1] != '/')
        strcat(fpath, "/");
    for(int seg = 0; seg < maxseg; ++seg)
    {
        for(int snap = 0; snap < maxsnap; ++snap)
        {
            int fd = 0;
            unsigned char * map = NULL;

            /* Check for file */
            snprintf(fname, NAMELEN, "%s%d%s%d%s%d%s%d%s", "pid", pid, "_snap", snap + 1, "_snap", snap + 2 , "_seg", seg, ".memdiff");
            if(strcmp(fname, flist[curfile]->d_name) != 0)
                continue;
            
            /* Format name */
            strcat(fpath, flist[curfile]->d_name);

            /* Open file */
            fd = open(fpath, 0);
            err_chk(fd == -1);
            chk = fstat(fd, &statchk);
            err_chk(chk == -1);
            if(statchk.st_size == 0)
                goto dataloadloopcleanup;
            
            /* Process file */
            data[seg][snap].size = statchk.st_size;
            map = mmap(NULL, statchk.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            err_chk(map == NULL);
            data[seg][snap].ones = popcounter(map, statchk.st_size);

dataloadloopcleanup:
            /* Cleanup */
            rindex(fpath, '/')[1] = '\0';
            if(map)
                chk = munmap(map, statchk.st_size);
            err_chk(chk == -1);
            if(fd)
                chk = close(fd);
            err_chk(chk == -1);
            curfile++;
        }
    }

    /* Output results */
    if(!OPT_Q && OPT_L)
        printf("Showing regions above %d blocks only:\n", limit);
    else if(!OPT_Q && !OPT_U)
        printf("Changed regions only:\n");

    snapcols = calloc(maxsnap, sizeof(int *));
    printf("Region |    Total region changes    |");
    for(int snap = 0; snap < maxsnap; ++snap)
        snapcols[snap] = printf("   Changed snap%d to snap%d   |", snap + 1, snap + 2);
    printf("\n");
    for(int seg = 0; seg < maxseg; ++seg)
    {
        unsigned long long totalones = 0;
        unsigned long long totalsize = 0;
        int oc = 0; /* output character count */

        for(int snap = 0; snap < maxsnap; ++snap)
        {
            totalsize += data[seg][snap].size;
            totalones += data[seg][snap].ones;
        }
        if(!OPT_U && totalones == 0)
            continue;
        if(totalsize == 0)
            continue;
        if(OPT_L && (totalsize / maxsnap) < limit)
            continue;

        /* Segment result */
        printf("%6d |", seg);
        oc = printf(" %lld / %lld ", totalones, totalsize * 8);
        snprintf(fname, NAMELEN, "%%%d.2f%% |", 28 - oc - 2); /* reuse fname as format string buffer */
        printf(fname, (double) ((double) totalones * 100 / (double) (totalsize * 8)));
        for(int snap = 0; snap < maxsnap; ++snap)
        {
            if(data[seg][snap].size == 0)
            {
                snprintf(fname, NAMELEN, "%%%ds |", snapcols[snap] - 2);
                printf(fname, " ");
                continue;
            }
            oc = printf(" %lld / %lld ", data[seg][snap].ones, data[seg][snap].size * 8);
            snprintf(fname, NAMELEN, "%%%d.2f%% |", snapcols[snap] - oc - 3);
            printf(fname, (double) ((double) data[seg][snap].ones * 100 / (double) (data[seg][snap].size * 8)));
        }
        printf("\n");
    }

    success = true;

    (void)(blocksize); /* variable unused in this version */

err:
    if(errno)
        perror("memxamine");
    /* srcdir sometimes leaks two bytes */
    if(base)
        free(base);
    if(data)
        free(data);
    if(snapcols)
        free(snapcols);
    if(!success)
        exit(EXIT_FAILURE);
    exit(EXIT_SUCCESS);
}

/* Count ones */
unsigned long long popcounter(unsigned char * start, unsigned long long len)
{
    unsigned char * cur;
    unsigned long long ones = 0;

    /* XXX This could be a lot faster if you throw 8 bytes into the intrinsic instead of 1... */
    for(cur = start; cur < (start + len); ++cur)
    {
        ones += __builtin_popcount(*cur);
    }

    return ones;
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
