#include "harness/trace.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

int harness_debug_level = 4;

void debug_printf(const char* fmt, const char* fn, const char* fmt2, ...) {
    va_list ap;

    printf(fmt, fn);

    va_start(ap, fmt2);
    vprintf(fmt2, ap);
    va_end(ap);

    puts("\033[0m");
    /* stdout is fully buffered when redirected to a file or pipe, so a run that
     * is stopped rather than exited loses every line. Test harnesses do exactly
     * that. */
    fflush(stdout);
}

void panic_printf(const char* fmt, const char* fn, const char* fmt2, ...) {
    va_list ap;

    FILE* fp = fopen("dethrace.log", "w");

    puts("\033[0;31m");
    printf(fmt, fn);

    if (fp != NULL) {
        fprintf(fp, fmt, fn);
    }

    va_start(ap, fmt2);
    vprintf(fmt2, ap);
    if (fp != NULL) {
        vfprintf(fp, fmt2, ap);
    }
    va_end(ap);
    if (fp != NULL) {
        fclose(fp);
    }
    puts("\033[0m");
}
