// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
/*
finddupes -- finds duplicate files in a given set of directories

Copyright (C) 2014 Jesús Ruiz de Infante

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "klib/khash.h"
#include "klib/klist.h"
#include "md5/md5.h"

//#define printd(...) fprintf(stderr, __VA_ARGS__)
#define printd(...) /* nothing */

#define CHUNK_SIZE 8192
#define PARTIAL_MD5_SIZE 4096
#define __nop_free(x)

struct inodev {
    ino_t ino;
    dev_t dev;
};

KLIST_INIT(str, const char *, __nop_free)
KLIST_INIT(inodev, struct inodev, __nop_free)
KHASH_MAP_INIT_STR(str, klist_t(str)*)

const char VERSION[] = "0.2";
int flags;
char *sep = "\n";
size_t seplen = 1;
char *setsep = "\n\n";
size_t setseplen = 2;

enum {
    F_OMITFIRST         =  1 << 1,
    F_RECURSE           =  1 << 2,
    F_HIDEPROGRESS      =  1 << 3,
    F_FOLLOWLINKS       =  1 << 4,
    F_CONSIDERHARDLINKS =  1 << 5,
    F_EXCLUDEEMPTY      =  1 << 6,
    F_UNIQUE            =  1 << 7,
    F_SEPARATOR         =  1 << 8,
    F_SETSEPARATOR      =  1 << 9,
};

int fromhex(unsigned char c)
{
    if (!isxdigit(c))
        return -1;
    static const char hexdigits[] = "0123456789abcdef";
    return (int)(strchr(hexdigits, tolower(c)) - hexdigits);
}

char *unescapestr(char *s, size_t *len, int *err)
{
    unsigned char *str = (unsigned char*)s;
    unsigned char *dest = str;
    unsigned char c;
    int error = 0;

    while ((c = *str++)) {
        if (c == '\\') {
            switch (c = *str++) {
            case 'a':
                *dest++ = '\a';
                break;
            case 'b':
                *dest++ = '\b';
                break;
            case 'f':
                *dest++ = '\f';
                break;
            case 'n':
                *dest++ = '\n';
                break;
            case 'r':
                *dest++ = '\r';
                break;
            case 't':
                *dest++ = '\t';
                break;
            case 'v':
                *dest++ = '\v';
                break;
            case '\\':
                *dest++ = '\\';
                break;
            case '\'':
                *dest++ = '\'';
                break;
            case '\"':
                *dest++ = '\"';
                break;
            case '\?':
                *dest++ = '\?';
                break;
            case 'x': {
                int d = fromhex(*str++);
                if (d != -1) {
                    int d2 = fromhex(*str++);
                    if (d2 != -1) {
                        *dest++ = 16*d + d2;
                        break;
                    }
                }
                error = EINVAL;
                goto out;
            }
            default:
                if (c >= '0' && c < '8') {
                    int d = c - '0';
                    c = *str++;
                    if (c >= '0' && c < '8') {
                        d = d*8 + c - '0';
                        c = *str++;
                        if (c >= '0' && c < '8') {
                            *dest++ = d*8 + c - '0';
                            break;
                        }
                    }
                    error = EINVAL;
                    goto out;
                } else {
                    *dest++ = '\\';
                    *dest++ = c;
                }
            }
        } else
            *dest++ = c;
    }
out:
    *dest = '\0';
    if (len)
        *len = (char*)dest - s;
    if (err)
        *err = error;
    return s;
}

void errormsg(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
}

void usage(void)
{
    fputs("usage: finddupes [options] PATH...\n\n"
          " -r --recursive   \tfor every directory given follow subdirectories\n"
          "                  \tencountered within\n"
          " -s --symlinks    \tfollow symlinks\n"
          " -H --hardlinks   \tnormally, when two or more files point to the same\n"
          "                  \tdisk area they are treated as non-duplicates; this\n"
          "                  \toption will change this behavior\n"
          " -n --noempty     \texclude zero-length files from consideration\n"
          " -f --omitfirst   \tomit the first file in each set of matches\n"
          " -u --unique      \tlist only files that don't have duplicates\n"
          " -q --quiet       \thide progress indicator\n"
          " -p --separator=sep\tseparate files with sep string instead of '\\n'\n"
          " -P --setseparator=sep  separate sets with sep string instead of '\\n\\n'\n"
          " -v --version     \tdisplay finddupes version\n"
          " -h --help        \tdisplay this help message\n", stderr);
}

char *normalizepath(const char *path)
{
    char *p = strdup(path);
    int last = strlen(p) - 1;
    if (last >= 0 && p[last] == '/')
        /* avoid problems lstat()ing path if it is a dir ending with '/' */
        p[last] = '\0';
    return p;
}

char *joinpath(const char *dir, const char *filename)
{
    char *fpath = malloc(strlen(dir) + strlen(filename) + 2);
    strcpy(fpath, dir);
    int last = strlen(dir) - 1;
    if (last >= 0 && dir[last] != '/')
        strcat(fpath, "/");
    strcat(fpath, filename);

    return fpath;
}

char *getsignatureuntil(const char *filename, off_t max_read, off_t fsize)
{
//    printd("-- %s filename %s\n", __func__, filename);

    md5_state_t state;
    md5_byte_t digest[16];

    md5_init(&state);

    // always include file size in the signature
    md5_append(&state, (md5_byte_t*)&fsize, sizeof fsize);

    if (filename) { // include (partial) file contents only if asked to
        off_t toread;
        static md5_byte_t chunk[CHUNK_SIZE];
        FILE *file;

        if (max_read != 0 && fsize > max_read)
            fsize = max_read;

        file = fopen(filename, "rb");
        if (file == NULL) {
            errormsg("error opening file %s\n", filename);
            return NULL;
        }

        while (fsize > 0) {
            toread = (fsize % CHUNK_SIZE) ? (fsize % CHUNK_SIZE) : CHUNK_SIZE;
            if (fread(chunk, toread, 1, file) != 1) {
                errormsg("error reading from file %s\n", filename);
                fclose(file);
                return NULL;
            }
            md5_append(&state, chunk, toread);
            fsize -= toread;
        }

        fclose(file);
    }

    md5_finish(&state, digest);

    char signature[16*2 + 1];
    char *sigp = signature;
    static const char hexdigits[] = "0123456789abcdef";
    for (int x = 0; x < 16; x++) {
        md5_byte_t digit0 = digest[x] % 16;
        md5_byte_t digit1 = (digest[x] - digit0) / 16;
        *sigp++ = hexdigits[digit1];
        *sigp++ = hexdigits[digit0];
    }
    *sigp = '\0';

    return strdup(signature);
}

char *getfullsignature(const char *filename, off_t fsize)
{
    return getsignatureuntil(filename, 0, fsize);
}

char *getpartialsignature(const char *filename, off_t fsize)
{
    return getsignatureuntil(filename, PARTIAL_MD5_SIZE, fsize);
}

char *getfilesizesignature(off_t fsize)
{
    return getsignatureuntil(NULL, 0, fsize);
}

/**
 * @param fpath a heap allocated string; the function takes ownership of fpath
 */
void grokfile(const char *fpath, const struct stat *info, khash_t(str) *files)
{
//    printd("-- %s %s\n", __func__, fpath);

    struct stat linfo;

    static const char indicator[] = "-\\|/";
    static int progress = 0;
    if (!(flags & F_HIDEPROGRESS)) {
        fprintf(stderr, "\rscanning files %c ", indicator[progress]);
        progress = (progress + 1) % 4;
    }

    if (lstat(fpath, &linfo) == -1) {
        errormsg("lstat failed: %s: %s\n", fpath, strerror(errno));
        return;
    }

    if (!(S_ISREG(linfo.st_mode)
            || (S_ISLNK(linfo.st_mode) && flags & F_FOLLOWLINKS))) {
        printd("-- %s skipping non-regular or symlink file %s\n", __func__, fpath);
        goto out2;
    }

    if (info->st_size == 0 && flags & F_EXCLUDEEMPTY) {
        printd("-- %s skipping empty file %s\n", __func__, fpath);
        goto out2;
    }

    const char *sig = getfilesizesignature(info->st_size);
    if (!sig) {
        goto out2;
    }
//        printd("-- %s %s %u %s\n", __func__, sig, (unsigned)strlen(sig), fpath);

    int ret;
    khiter_t k = kh_put(str, files, sig, &ret);
//        printd("-- %s kh_put sig %s ret %d\n", __func__, sig, ret);

    klist_t(str) *dupes;

    switch (ret) {
    case -1:
        errormsg("%s error in kh_put()\n", __func__);
        goto out1;
    case 0:
//            printd("-- %s key already present\n", __func__);
        free((char*)sig);
        dupes = kh_value(files, k);
        break;
    default:
        dupes = kl_init(str);
        kh_value(files, k) = dupes;
        break;
    }

    *kl_pushp(str, dupes) = fpath;
    return;

out1:
    free((char*)sig);
out2:
    free((char*)fpath);
}

void grokdir(const char *dir, khash_t(str) *files)
{
//    printd("-- %s %s\n", __func__, dir);

    DIR *cd;
    struct dirent *dirinfo;
    struct stat info;
    struct stat linfo;

    if (lstat(dir, &linfo) == -1) {
        errormsg("lstat failed: %s: %s\n", dir, strerror(errno));
        return;
    }

    if (!(flags & F_FOLLOWLINKS) && S_ISLNK(linfo.st_mode))
        return;

    cd = opendir(dir);
    if (!cd) {
        errormsg("could not chdir to %s: %s\n", dir, strerror(errno));
        return;
    }

    while ((dirinfo = readdir(cd)) != NULL) {
        if (strcmp(dirinfo->d_name, ".") == 0
                || strcmp(dirinfo->d_name, "..") == 0)
            continue;

        const char *fpath = joinpath(dir, dirinfo->d_name);

        if (stat(fpath, &info) == -1) {
            errormsg("stat failed: %s: %s\n", fpath, strerror(errno));
            free((char*)fpath);
            continue;
        }

        if (S_ISDIR(info.st_mode)) {
            if (flags & F_RECURSE)
                grokdir(fpath, files);
            free((char*)fpath);
        } else
            grokfile(fpath, &info, files);
    }
    closedir(cd);
}

/**
 * move files for which the signature given by signaturefunction
 * differs from the current partial signature from files to checked_files
 */
void checkdupes(khint_t k, khash_t(str) *files, khash_t(str) *checked_files,
    char *(*signaturefunction)(const char *filename, off_t fsize))
{
//    printd("%s files[%s]\n", __func__, kh_key(files, k));
    klist_t(str) *dupes = kh_value(files, k);
    kliter_t(str) *p;

    if (kl_begin(dupes) == kl_end(dupes) // empty?
            || kl_next(kl_begin(dupes)) == kl_end(dupes)) { // size == 1?
//        printd("%s no dupes for %s\n", __func__, kh_key(files, k));
        return;
    }

    const char *partsig = kh_key(files, k); // current partial signature
    klist_t(str) *filtered_dupes = kl_init(str);
    struct stat info;

    for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p)) {
        const char *fpath = kl_val(p);

        if (stat(fpath, &info) == -1) {
            errormsg("stat failed: %s: %s\n", fpath, strerror(errno));
            continue;
        }

        const char *newsig = signaturefunction(fpath, info.st_size);
        if (!newsig)
            continue;

//        printd("-- %s %s newsig %s\n", __func__, fpath, newsig);

        if (strcmp(newsig, partsig) == 0) {
//            printd("-- %s newsig == partsig, continuing\n", __func__);
            free((char*)newsig);
            *kl_pushp(str, filtered_dupes) = fpath;
            continue;
        }

        int ret;
        khiter_t checked_k = kh_put(str, checked_files, newsig, &ret);
//        printd("-- %s kh_put newsig %s ret %d\n", __func__, newsig, ret);

        klist_t(str) *checked_dupes;

        switch (ret) {
        case -1:
            errormsg("%s error in kh_put()\n", __func__);
            free((char*)newsig);
            continue;
        case 0:
//            printd("-- %s key already present\n", __func__);
            free((char*)newsig);
            checked_dupes = kh_value(checked_files, checked_k);
            break;
        default:
            checked_dupes = kl_init(str);
            kh_value(checked_files, checked_k) = checked_dupes;
            break;
        }

        *kl_pushp(str, checked_dupes) = fpath;
    }

    if (kl_begin(filtered_dupes) == kl_end(filtered_dupes)) {
        // filtered_dupes is empty; remove files entry at k
        kh_del(str, files, k);
        free((char*)partsig);
        kl_destroy(str, filtered_dupes);
    } else // replace files entry at k
        kh_value(files, k) = filtered_dupes;

    kl_destroy(str, dupes);
}

/**
 * remove from the list at k all paths pointing to the same inode and device,
 * except the first occurrence
 */
void checkinodes(khint_t k, khash_t(str) *files)
{
//    printd("%s files[%s]\n", __func__, kh_key(files, k));
    klist_t(str) *dupes = kh_value(files, k);
    kliter_t(str) *p;

    if (kl_begin(dupes) == kl_end(dupes) // empty?
            || kl_next(kl_begin(dupes)) == kl_end(dupes)) { // size == 1?
//        printd("%s no dupes for %s\n", __func__, kh_key(files, k));
        return;
    }

    struct stat info;
    klist_t(inodev) *inodes = kl_init(inodev);
    klist_t(str) *filtered_dupes = kl_init(str);

    for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p)) {
        const char *fpath = kl_val(p);

        if (stat(fpath, &info) == -1) {
            errormsg("stat failed: %s: %s\n", fpath, strerror(errno));
            continue;
        }

        kliter_t(inodev) *i;
        for (i = kl_begin(inodes); i != kl_end(inodes); i = kl_next(i))
            if (kl_val(i).ino == info.st_ino && kl_val(i).dev == info.st_dev)
                break;

        if (i == kl_end(inodes)) {
            printd("-- %s found new inode %llu pointing at %s\n",
                   __func__, (unsigned long long)(info.st_ino), fpath);
            struct inodev id = { info.st_ino, info.st_dev };
            *kl_pushp(inodev, inodes) = id;
            *kl_pushp(str, filtered_dupes) = fpath;
        } else {
            if (flags & F_FOLLOWLINKS) {
                if (lstat(fpath, &info) == -1) {
                    errormsg("lstat failed: %s: %s\n", fpath, strerror(errno));
                    free((char*)fpath);
                    continue;
                }
                if (S_ISLNK(info.st_mode)) {
                    //  duped symlinks are always listed if the --symlinks
                    //  option is set
                    *kl_pushp(str, filtered_dupes) = fpath;
                    continue;
                }
            }
            printd("-- %s inode %llu already seen, removing %s from dupes\n",
                   __func__, (unsigned long long)(info.st_ino), fpath);
            free((char*)fpath);
        }
    }

    // replace files entry at k
    kh_value(files, k) = filtered_dupes;

    kl_destroy(inodev, inodes);
    kl_destroy(str, dupes);
}

/**
 * merge checked_files into files
 */
void mergechecked(khash_t(str) *files, khash_t(str) *checked_files)
{
    khint_t checked_k;
    for (checked_k = kh_begin(checked_files);
            checked_k != kh_end(checked_files); ++checked_k) {
        if (!kh_exist(checked_files, checked_k))
            continue;

        const char *sig = kh_key(checked_files, checked_k);
        int ret;
        khiter_t k = kh_put(str, files, sig, &ret);
//        printd("-- %s kh_put sig %s ret %d\n", __func__, fullsig, ret);

        switch (ret) {
        case -1:
            errormsg("%s error in kh_put()\n", __func__);
            continue;
        case 0:
            errormsg("-- %s uh oh key already present %s\n", __func__, sig);
            // there is a file with a (partial) signature equal to the current
            // full checked signature
            // TODO fix signature collision
            continue;
        default:
            kh_value(files, k) = kh_value(checked_files, checked_k);
            break;
        }
    }
    kh_clear(str, checked_files);
}

void dumpfiles(khash_t(str) *files)
{
    khint_t k;
    for (k = kh_begin(files); k != kh_end(files); ++k)
        if (kh_exist(files, k)) {
            printd("%s files[%s]\n", __func__, kh_key(files, k));
            klist_t(str) *dupes = kh_value(files, k);
            kliter_t(str) *p;
            for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p))
                printd("\t%s\n", kl_val(p));
        }
}

/**
 * free files's hash keys (C strings) and values (lists of C strings)
 */
void freefiles(khash_t(str) *files)
{
    khint_t k;
    for (k = kh_begin(files); k != kh_end(files); ++k)
        // explicitly freeing memory takes 10-20% CPU time.
        if (kh_exist(files, k)) {
            klist_t(str) *dupes = kh_value(files, k);
            kliter_t(str) *p;
            for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p))
                free((char*)kl_val(p));
            kl_destroy(str, dupes);
            free((char*)kh_key(files, k));
        }
}

void putverbatim(const char *str, size_t len)
{
    while(len--)
        putchar(*str++);
}

void printfiles(khash_t(str) *files)
{
    khint_t k;
    for (k = kh_begin(files); k != kh_end(files); ++k) {
        if (!kh_exist(files, k))
            continue;
        klist_t(str) *dupes = kh_value(files, k);
        if (kl_begin(dupes) == kl_end(dupes))
            continue;
        if (kl_next(kl_begin(dupes)) == kl_end(dupes)) { // size == 1?
            if (flags & F_UNIQUE) {
                fputs(kl_val(kl_begin(dupes)), stdout);
                putverbatim(sep, seplen);
            }
            continue;
        } else {
            if (flags & F_UNIQUE)
                continue;
            kliter_t(str) *p;
            for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p)) {
                if (flags & F_OMITFIRST && p == kl_begin(dupes))
                    continue;
                fputs(kl_val(p), stdout);
                if (kl_next(p) != kl_end(dupes))
                    putverbatim(sep, seplen);
            }
            putverbatim(setsep, setseplen);
        }
    }
}

int parseopts(int argc, char **argv)
{
    static struct option long_options[] = {
        { "omitfirst",     0,                  NULL,  'f' },
        { "recursive",     0,                  NULL,  'r' },
        { "quiet",         0,                  NULL,  'q' },
        { "unique",        0,                  NULL,  'u' },
        { "symlinks",      0,                  NULL,  's' },
        { "hardlinks",     0,                  NULL,  'H' },
        { "noempty",       0,                  NULL,  'n' },
        { "version",       0,                  NULL,  'v' },
        { "help",          0,                  NULL,  'h' },
        { "separator",     required_argument,  NULL,  'p' },
        { "setseparator",  required_argument,  NULL,  'P' },
        { NULL,            0,                  NULL,  0 }
    };

    int opt;

    while ((opt = getopt_long(argc, argv, "frqusHnvhp:P:",
                              long_options, NULL)) != EOF) {
        switch (opt) {
        case 'f':
            flags |= F_OMITFIRST;
            break;
        case 'r':
            flags |= F_RECURSE;
            break;
        case 'q':
            flags |= F_HIDEPROGRESS;
            break;
        case 'u':
            flags |= F_UNIQUE;
            break;
        case 's':
            flags |= F_FOLLOWLINKS;
            break;
        case 'H':
            flags |= F_CONSIDERHARDLINKS;
            break;
        case 'n':
            flags |= F_EXCLUDEEMPTY;
            break;
        case 'v':
            printf("finddupes %s\n", VERSION);
            exit(0);
        case 'h':
            usage();
            exit(0);
        case 'p': {
            int err;
            unescapestr(optarg, &seplen, &err);
            printd("-p sep '%s' len %zu err %d\n", optarg, seplen, err);
            if (err) {
                errormsg("invalid format in separator string\n");
                exit(1);
            }
            sep = malloc(seplen+1);
            memcpy(sep, optarg, seplen+1);
            flags |= F_SEPARATOR;
            break;
        }
        case 'P': {
            int err;
            unescapestr(optarg, &setseplen, &err);
            printd("-P setsep '%s' len %zu err %d\n", optarg, setseplen, err);
            if (err) {
                errormsg("invalid format in setseparator string\n");
                exit(1);
            }
            setsep = malloc(setseplen+1);
            memcpy(setsep, optarg, setseplen+1);
            flags |= F_SETSEPARATOR;
            break;
        }

        default:
            fprintf(stderr, "Try `finddupes --help' for more information.\n");
            exit(1);
        }
    }

    if (optind >= argc) {
        errormsg("no paths specified\n");
        exit(1);
    }

    return optind;
}

int main(int argc, char **argv)
{
    int firstarg = parseopts(argc, argv);
    printd("-- %s firstarg %d flags 0x%x\n", __func__, firstarg, flags);

    khash_t(str) *files = kh_init(str);

    struct stat info;
    // first pass: get file size signature
    for (int i = firstarg; i < argc; ++i) {
        if (stat(argv[i], &info) == -1) {
            errormsg("stat failed: %s: %s\n", argv[i], strerror(errno));
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            char *dir = normalizepath(argv[i]);
            grokdir(dir, files);
            free(dir);
        } else
            grokfile(strdup(argv[i]), &info, files);
    }

    if (!(flags & F_HIDEPROGRESS))
        fprintf(stderr, "\r%40s\r", " ");

//    printd("-- after first pass: getfilesizesignature\n");
//    dumpfiles(files);

    khash_t(str) *checked_files = kh_init(str);

    // second pass: get partial signature (check the first bytes of the file)
    for (khint_t k = kh_begin(files); k != kh_end(files); ++k)
        if (kh_exist(files, k))
            checkdupes(k, files, checked_files, getpartialsignature);
    mergechecked(files, checked_files);

//    printd("-- after second pass: getpartialsignature\n");
//    dumpfiles(files);

    // third pass: get full contents signature
    for (khint_t k = kh_begin(files); k != kh_end(files); ++k)
        if (kh_exist(files, k))
            checkdupes(k, files, checked_files, getfullsignature);
    mergechecked(files, checked_files);

//    printd("-- after third pass: getfullsignature\n");
//    dumpfiles(files);

    if (!(flags & F_CONSIDERHARDLINKS))
        for (khint_t k = kh_begin(files); k != kh_end(files); ++k)
            if (kh_exist(files, k))
                checkinodes(k, files);

//    printd("-- after checkinodes\n");
//    dumpfiles(files);

    printfiles(files);

    freefiles(checked_files);
    kh_destroy(str, checked_files);

    freefiles(files);
    kh_destroy(str, files);

    if (flags & F_SEPARATOR)
        free(sep);
    if (flags & F_SETSEPARATOR)
        free(setsep);

    return 0;
}
