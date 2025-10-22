#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include "shell.h"

size_t page_size;

int main(int argc, char **argv) {
    page_size = sysconf(_SC_PAGESIZE);
    size_t buff_size = page_size;
    char *restrict buffer = get_buffer();
    size_t argv_size = page_size;
    char **restrict argvs = get_buffer();
    while (1) {
        write_prompt();
        fill_buffer(buffer, &buff_size);
        if (buffer == (void*)-1) {
            continue;
        }
        argv_size = get_argv(buffer, &buff_size, argvs, argv_size);
        if (argv_size == -1) {
            goto end;
        }
        if (match_builtin(argvs) != -1) {
            goto end;
        }
        char *pathname = match_path(argvs[0]);
        if (pathname == NULL) {
            goto end;
        }
        complete_path(pathname, argvs[0]);
        if (exec_child(pathname, argvs) == 1) {
            goto end;
        }
end:
        if (resize_buffer(buffer, buff_size, page_size) != (void*)-1) {
            buff_size = page_size;
        }
        buffer[buff_size-1] = 0;
        resize_buffer(argvs, argv_size, page_size);
        memset(buffer, 0, buff_size);
        memset(argvs, 0, argv_size);
    }
    free_buffer(argvs, argv_size);
    free_buffer(buffer, buff_size);

    return 0;
}
