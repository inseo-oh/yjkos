# ls(1)

## NAME

ls - list directory contents

## SYNOPSIS

```shell
ls [options] [file...]
```

## DESCRIPTION

Shows list of directory contents.

By default files starting with `.` are not shown. Use `-a` or `-A` option to override it.

Available options are:

- `-a`: Show files beginning with `.`, including `.` and `..`
- `-A`: Same as `-a`, but excludes `.` and `..`

- `-m`: Display as comma-separated list

## SPECIFICATION

This utility implements part of POSIX specification. See [POSIX specification for this utility](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html)
