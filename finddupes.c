// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

#include <assert.h>
#include <dirent.h>
#include <errno.h>
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

#define printd printf

#define CHUNK_SIZE 8192
#define PARTIAL_MD5_SIZE 4096
#define __str_free(x)

KLIST_INIT(str, const char *, __str_free);
KHASH_MAP_INIT_STR(str, klist_t(str)*)

int flags;

enum {
    F_RECURSE = 1,
    F_FOLLOWLINKS = 1 << 2,
};

void errormsg(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
}

// TODO replace malloc w/ checkmalloc in the whole program
// e.g. also for strcat()
void *checkmalloc(size_t size)
{
    void *r = malloc(size);
    if (!r) {
        errormsg("out of memory!\n");
        exit(1);
    }
    return r;
}

void usage(void)
{
    fprintf(stderr, "usage: fdupes2 DIR...\n");
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
    char *fpath = (char*)checkmalloc(strlen(dir)
                                     + strlen(filename)+2);
    strcpy(fpath, dir);
    int last = strlen(dir) - 1;
    if (last >= 0 && dir[last] != '/')
        strcat(fpath, "/");
    strcat(fpath, filename);

    return fpath;
}

off_t filesize(const char *filename)
{
    struct stat s;
    if (stat(filename, &s) != 0) {
        errormsg("stat failed: %s: %s\n", filename, strerror(errno));
        return -1;
    }
    return s.st_size;
}

char *getsignatureuntil(const char *filename, off_t max_read)
{
//    printd("-- %s filename %s\n", __func__, filename);

    int x;
    off_t fsize;
    off_t toread;
    md5_state_t state;
    md5_byte_t digest[16];
    static md5_byte_t chunk[CHUNK_SIZE];
    char signature[16*2 + 1] = "";
    char *sigp;
    FILE *file;

    md5_init(&state);

    fsize = filesize(filename);

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

    md5_finish(&state, digest);

    sigp = signature;

    for (x = 0; x < 16; x++) {
        sprintf(sigp, "%02x", digest[x]);
        sigp = strchr(sigp, '\0');
    }

    fclose(file);

    return strdup(signature);
}

char *getsignature(const char *filename)
{
    return getsignatureuntil(filename, 0); // TODO
}

char *getpartialsignature(const char *filename)
{
    return getsignatureuntil(filename, PARTIAL_MD5_SIZE);
}

void grokfile(const char *fpath, khash_t(str) *files)
{
    printd("-- %s %s\n", __func__, fpath);

    struct stat linfo;

    if (lstat(fpath, &linfo) == -1) {
        errormsg("lstat failed: %s: %s\n", fpath, strerror(errno));
        return;
    }

    if (S_ISREG(linfo.st_mode)
            || (S_ISLNK(linfo.st_mode) && flags & F_FOLLOWLINKS)) {
        const char *sig = getpartialsignature(fpath);
        if (!sig)
            return;
//            printd("-- %s %s %u %s\n", __func__, sig, (unsigned)strlen(sig), fpath);

        int ret;
        khiter_t k = kh_put(str, files, sig, &ret);
        printd("-- %s kh_put sig %s ret %d\n", __func__, sig, ret);

        klist_t(str) *dupes;

        switch (ret) {
        case -1:
            errormsg("%s error in kh_put()\n", __func__);
            return;
        case 0:
            printd("-- %s key already present\n", __func__);
            dupes = kh_value(files, k);
            break;
        default:
            dupes = kl_init(str);
            kh_value(files, k) = dupes;
            break;
        }

        *kl_pushp(str, dupes) = fpath;
    }
}

void grokdir(const char *dir, khash_t(str) *files)
{
    printd("-- %s %s\n", __func__, dir);

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
            continue;
        }

        if (S_ISDIR(info.st_mode)) {
            grokdir(fpath, files);
        } else
            grokfile(fpath, files);
    }
    closedir(cd);
}


#define __int_free(x)
KLIST_INIT(32, int, __int_free)
void test_klist(void)
{
    {
        klist_t(32) *kl;
        kliter_t(32) *p;
        kl = kl_init(32);
        *kl_pushp(32, kl) = 1;
        *kl_pushp(32, kl) = 10;
//	kl_shift(32, kl, 0);
        for (p = kl_begin(kl); p != kl_end(kl); p = kl_next(p))
            printf("%d\n", kl_val(p));
        kl_destroy(32, kl);
    }

    klist_t(str) *dupes;
    kliter_t(str) *p;
    dupes = kl_init(str);
    *kl_pushp(str, dupes) = strdup("asdf");
    *kl_pushp(str, dupes) = strdup("qwertz");
    for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p))
        printf("%s\n", kl_val(p));
    kl_destroy(str, dupes);
}

int main(int argc, char **argv)
{
//    test_klist();
//    return 0;

    if (argc < 2) {
        usage();
        return 1;
    }

    flags |= F_RECURSE;
    flags |= F_FOLLOWLINKS;

    khash_t(str) *files = kh_init(str);

    int i;
    struct stat info;
    for (i = 1; i < argc; ++i) {
        if (stat(argv[i], &info) == -1) {
            errormsg("stat failed: %s: %s\n", argv[i], strerror(errno));
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            char *dir = normalizepath(argv[i]);
            grokdir(dir, files);
            free(dir);
        } else
            grokfile(strdup(argv[i]), files);
    }

    khint_t k;
    for (k = kh_begin(files); k != kh_end(files); ++k)
        if (kh_exist(files, k)) {
            printd("%s files[%s]\n", __func__, kh_key(files, k));
            klist_t(str) *dupes = kh_value(files, k);
            kliter_t(str) *p;
            for (p = kl_begin(dupes); p != kl_end(dupes); p = kl_next(p))
                printf("\t%s\n", kl_val(p));
        }

    for (k = kh_begin(files); k != kh_end(files); ++k)
        // explicitly freeing memory takes 10-20% CPU time.
        if (kh_exist(files, k)) {
            klist_t(str) *dupes = kh_value(files, k);
            kl_destroy(str, dupes);
        }

    kh_destroy(str, files);

    return 0;
}
