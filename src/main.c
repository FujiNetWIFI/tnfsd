/* The MIT License
 *
 * Copyright (c) 2010 Dylan Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * The main()
 *
 * */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "chroot.h"
#include "log.h"
#include "tnfsd.h"

/* declare the main() - it won't be used elsewhere so I'll not bother
 * with putting it in a .h file */
int main(int argc, char **argv);

void print_usage();

int main(int argc, char **argv)
{
    int opt;
#ifdef ENABLE_CHROOT
    char *uvalue = NULL;
    char *gvalue = NULL;
#endif
    bool read_only = false;
    char *pvalue = NULL;
    char *root_path = NULL;

    #ifdef ENABLE_CHROOT
    while((opt = getopt(argc, argv, "ru:g:p:")) != -1)
    #else
    while((opt = getopt(argc, argv, "rp:")) != -1)
    #endif
    {
        switch(opt)
        {
            case 'p':
                pvalue = optarg;
                break;
            case 'r':
                read_only = true;
                break;
            #ifdef ENABLE_CHROOT
            case 'u':
                uvalue = optarg;
                break;
            case 'g':
                gvalue = optarg;
                break;
            #endif
            case ':':
                fprintf(stderr, "option needs a value\n");
                print_usage();
                exit(-1);
                break;
            default:
            case '?':
                fprintf(stderr, "unknown option: %c\n", optopt);
                print_usage();
                exit(-1);
                break;
        }
    }

    if (optind < argc)
    {
        root_path = argv[optind++];
    }
    if (optind < argc)
    {
        fprintf(stderr, "parameters after the root dir %s are not allowed\n", root_path);
        print_usage();
        exit(-1);
    }
    if (root_path == NULL)
    {
        fprintf(stderr, "please specify root dir\n");
        print_usage();
        exit(-1);
    }

    #ifdef ENABLE_CHROOT
    if (uvalue || gvalue)
    {
        /* chroot into the specified directory and drop privs */
        if (uvalue == NULL)
        {
            fprintf(stderr, "chroot username required\n");
            exit(-1);
        } else if (gvalue == NULL)
        {
            fprintf(stderr, "chroot group required\n");
            exit(-1);
        }
        fprintf(stderr, "tnfsd will be jailed at %s\n", root_path);
        chroot_tnfs(uvalue, gvalue, root_path);
        root_path = strdup("/");
    }
    warn_if_root();
    #endif

    int port = TNFSD_PORT;

    if (pvalue)
    {
        port = atoi(pvalue);
        if (port < 1 || port > 65535)
        {
            fprintf(stderr, "Invalid port\n");
            exit(-1);
        }
    }

    tnfsd_init();
    tnfsd_init_logs(STDERR_FILENO);
    signal(SIGINT, tnfsd_stop);
    tnfsd_start(root_path, port, read_only);

    return 0;
}

void print_usage()
{
    #ifdef ENABLE_CHROOT
    fprintf(stderr, "Usage: tnfsd [-u <username> -g <group> -p <port> -r] <root dir>\n");
    #else
    fprintf(stderr, "Usage: tnfsd [-p <port> -r] <root dir>\n");
    #endif
}