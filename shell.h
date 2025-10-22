#if defined(__unix__) && defined(_POSIX_VERSION)
#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

extern size_t page_size;

void *get_buffer(void);
void *resize_buffer(void *restrict buffer, size_t old_size, size_t new_size);
void free_buffer(void *buffer, size_t size);
void write_prompt(void);
void fill_buffer(char *restrict buffer, size_t *restrict size);
size_t get_argv(char *restrict buffer, size_t *restrict size, char **restrict argv, size_t argv_size);
char *match_path(const char *name);
void complete_path(char *path, char *name);
int exec_child(char *path, char **argv);
int match_builtin(char **argv);

int cd(char **argv);
int echo(char **argv);

#else
    #warning "System not posix compliant and/or unix based."
#endif
#endif
