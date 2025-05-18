#ifndef _UNISTD_H
#define _UNISTD_H

/* https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html */

#define _POSIX_VERSION 202405L
#define _POSIX2_VERSION 202405L

extern char *optarg;
extern int opterr, optind, optopt;

int getopt(int argc, char *const argv[], const char *optstring);

#endif
