#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "shell.h"

#if __has_include(<sys/mman.h>)
#include <sys/mman.h>
#endif

#ifdef __linux__
#include <linux/limits.h>
#define _GNU_SOURCE
#else
#define HOST_NAME_MAX sysconf(_SC_HOST_NAME_MAX)
#endif

extern char **environ;

void write_prompt(void) {
    char cwd[PATH_MAX];
    char hostname[HOST_NAME_MAX+1];
    char *username = getenv("USER");
    if (gethostname(hostname, HOST_NAME_MAX+1) == -1) {
        perror("shell in write_prompt at gethostname"); }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("shell in write_prompt at getcwd"); }
    if (username == NULL) {
        perror("getenv"); }
    printf("|%s@%s %s| ~> ", username, hostname, cwd);
    fflush(stdout);
    return;
}

#if defined(MAP_FAILED) && defined(PROT_READ) && defined (MAP_ANON)
void *get_buffer(void) {
    void *restrict buffer = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buffer != MAP_FAILED) {
        return buffer;
    }
    buffer = malloc(page_size);
    if (buffer == NULL) {
        perror("malloc");
    }
    return buffer;
}

void free_buffer(void *restrict buffer, size_t size) {
    if (munmap(buffer, size) == -1) {
        perror("munmap");
    }
}
#if defined(MREMAP_MAYMOVE)
void *resize_buffer(void *restrict buffer, size_t old_size, size_t new_size) {
    void *restrict buffertmp = mremap(buffer, old_size, new_size, MREMAP_MAYMOVE);
    if (buffertmp == MAP_FAILED) {
        perror("mremap");
        return MAP_FAILED;
    }
    return buffertmp;
}
#else
void *resize_buffer(void *restrict buffer, size_t old_size, size_t new_size) {
    void *restrict cp = mmap(NULL, old_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (cp == MAP_FAILED) {
        perror("mmap");
        return MAP_FAILED;
    }
    memcpy(cp, buffer, old_size);
    if (munmap(buffer, old_size) == -1) {
        perror("munmap");
        return MAP_FAILED;
    }
    buffer = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        return MAP_FAILED;
    }
    memcpy(buffer, cp, old_size);
    return buffer;
}
#endif
#else 
void *get_buffer(void) {
    void *restrict buffer = malloc(page_size);
    if (buffer == NULL) {
        perror("malloc");
    }
}

void free_buffer(void *restrict buffer, size_t size) {
    free(buffer);
}

void *resize_buffer(void *restrict buffer, size_t old_size, size_t new_size) {
    buffer = realloc(buffer, new_size);
    if (buffer == NULL) {
        perror("realloc");
        return (void)-1;
    }
    return buffer;
}
#endif

void fill_buffer(char *restrict buffer, size_t *restrict size) {
    buffer[*size-1] = 0; //canari
start:
    if (read(0, buffer+(*size-page_size), page_size) == -1) {
        perror("read");
        buffer = (void*)-1;
    }
    if (buffer[*size-1] != 0 && buffer[*size -1] != '\n') {
        if (resize_buffer(buffer, *size, *size+page_size) == (void*)-1) {
            buffer = (void*)-1;
            return;
        }
        *size += page_size;
        goto start;
    } else {
        return;
    }
}

size_t get_argv(char *restrict buffer, size_t *restrict size, char **restrict argv, size_t argv_size) {
    int argv_i = 0;
    size_t argv_len = page_size;
    for (size_t i = 0; buffer[i] != '\n'; ++i) {
        if (buffer[i] != 9 && buffer[i] != 32) {
            if (argv_i == argv_len-1) {
                argv = resize_buffer(argv, argv_len, argv_len+page_size);
                if (argv == (void*)-1) {
                    return -1;
                }
                argv_len += page_size;
            }
            argv[argv_i] = buffer+i;
            argv_i++;
            for (size_t j = i+1; ; ++j) {
                //printf("%d\n", j);
                if (buffer[j] == 9 || buffer[j] == 32 || buffer[j] == '\n') {
                    if (buffer[j] == '\n') {
                        buffer[j] = 0;
                    if (argv_i == argv_len-1) {
                        argv = resize_buffer(argv, argv_len, argv_len+page_size);
                        if (argv == (void*)-1) {
                            return -1;
                        }
                        argv_len += page_size;
                    }
                        argv[argv_i+1] = NULL;
                        return argv_len;
                    }
                    buffer[j] = 0;
                    i = j;
                    break;
                }
            }
        }
    }
    if (argv_i == 0) {
        return -1;
    }
    return argv_len;
}

char *match_path(const char *name) {
    char *PATH_dup = strdup(getenv("PATH"));
    errno = 0;
    int idx[2] = {0, 0};
    for (int i = 0; ; ++i) {
        if (PATH_dup[i] == ':' || PATH_dup[i] == 0) {
            idx[1] = i;
            PATH_dup[i] = 0;
            break;
        }
    }

    while (PATH_dup[idx[0]] != 0) {
        DIR *dirp = opendir(PATH_dup+idx[0]);
        if (dirp == NULL) {
            perror("opendir"); }
        struct dirent *entry = readdir(dirp);
        while (1) {
            if (entry == NULL) {
                if (errno != 0) {
                    perror("readdir");
                }
                break;
            }
            if (strcmp(entry->d_name, name) == 0) {
                switch (entry->d_type) {
                    case DT_REG:
                        return PATH_dup+idx[0];
                    case DT_DIR:
                        fprintf(stderr, "%s is a directory\n", name);
                        break;
                    case DT_BLK: 
                        fprintf(stderr, "%s is a block device\n", name);
                        break;
                    case DT_CHR:
                        fprintf(stderr, "%s is a character device\n", name);
                        break;
                    case DT_FIFO:
                        fprintf(stderr, "%s is a named pipe (FIFO)\n", name);
                        break;
                    case DT_SOCK:
                        fprintf(stderr, "%s is a socket\n", name);
                        break;
                    case DT_UNKNOWN:
                        fprintf(stderr, "%s is an unknown file type\n", name);
                        printf("Do you still want to execute it? y/n\n");
                        char *buff;
                        scanf("%1", buff);
                        if (*buff != 'y') {
                            break; }
                        return PATH_dup+idx[0]; 
                        break;
                }
            }
            entry = readdir(dirp);
        }
        for (int i = idx[1]+1; ; ++i) {
            PATH_dup[idx[1]] = ':';
            if (PATH_dup[i] == ':') {
                idx[0] = idx[1]+1;
                idx[1] = i;
                PATH_dup[i] = 0;
                break;
            }
            if (PATH_dup[i] == 0) {
                fprintf(stderr, "[shell] %s: command not found\n", name);
                return NULL;
            }
        }
    }

    return NULL;
}

void complete_path(char *path, char *name) {
    memcpy(path+(strlen(path)+1), name, strlen(name)+1);
    path[strlen(path)] = '/';
}

//this is ugly but uses the least memory
//and is as efficient as it gets
int match_builtin(char **argv) {
    int status;
    if (strcmp(argv[0], "cd") == 0) {
        status = cd(argv);
        return status;
    } else if (strcmp(argv[0], "exit") == 0) {
        exit(0);
    }
    return -1;
}

int exec_child(char *path, char **argv) {
    int status;
    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return 1;
    } else if (child == 0) {
        //in child
        if (execve(path, argv, environ) == -1) {
            perror("execve");
            return 1;
        }
    }
    wait(&status);
}

int cd(char **argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
        if (argc > 2) {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }
    }
    if (chdir(argv[1]) == -1) {
        perror("chdir");
        return 1;
    }
    return 0;
}
