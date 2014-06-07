// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

// https://stackoverflow.com/questions/262439/create-a-wrapper-function-for-malloc-and-free-in-c/4586534#4586534

#include <stdio.h>
#include <stdlib.h>

void *__real_malloc (size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);

void *
__wrap_malloc (size_t size)
{
//    printf ("malloc called with %zu\n", size);
    // size could be 0, in which case malloc would success but might
    // return NULL
    void *ret = __real_malloc (size);
    if (size && !ret) {
        fputs("Out of memory!\n", stderr);
        abort();
    }
    return ret;
}

void *
__wrap_calloc(size_t nmemb, size_t size)
{
//    printf ("calloc called with %zu %zu\n", nmemb, size);
    void *ret = __real_calloc (nmemb, size);
    if (size && nmemb && !ret) {
        fputs("Out of memory!\n", stderr);
        abort();
    }
    return ret;
}

void *__wrap_realloc(void *ptr, size_t size)
{
//    printf ("realloc called with %p %zu\n", ptr, size);
    void *ret = __real_realloc (ptr, size);
    if (size && !ret) {
        fputs("Out of memory!\n", stderr);
        abort();
    }
    return ret;
}
