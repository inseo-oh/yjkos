# runtest(1)

## NAME

runtest - Run YJKernel's unit tests.

## SYNOPSIS

```shell
runtest [options] [testgroups...]
```

## DESCRIPTION

Runs selected or all of YJKernel's built-in unit tests. Tests can be selected by specifying name of each test group. If you don't know what test groups are available, run `runtest -l`.  

Available options are:

- `-l`: Lists available test groups and exit.
- `-a`: Runs all testgroups. Manually specified groups are ignored.



