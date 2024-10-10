#pragma once
#include "lib/diagnostics.h"

typedef enum {
    OK = 0,

    // Errors derived from POSIX errno values
    ERR_PERM        = 1,  // Operation not permitted
    ERR_NOENT       = 2,  // No such file or directory
    ERR_IO          = 5,  // I/O error
    ERR_BADF        = 9,  // Bad filr descriptor
    ERR_NOMEM       = 12, // Not enough space
    ERR_FAULT       = 14, // Bad address
    ERR_NODEV       = 19, // No such device
    ERR_NOTDIR      = 20, // Not a directory
    ERR_ISDIR       = 21, // Is a directory
    ERR_INVAL       = 22, // Invalid argument
    ERR_NAMETOOLONG = 36, // File name too long
    ERR_NOTSUP      = 95, // Operation not supported

    // End of standard errno values
    ERR_STD_END,

    // YJK/OS specific errors
    ERR_SUBCMDDIED = 254, // Sub-command failed to run
    ERR_EOF        = 255, // End of file
} status_t;

// Failable functions are functions that may fail and return an error.
// Basic format of failable functions are:
// FAILABLE_FUNCTION MyFunction() {
// FAILABLE_PROLOGUE
//     (Main code here)
// FAILABLE_EPILOGUE_BEGIN
//     (Cleanup code here...)
// FAILABLE_EPILOGUE_END
// }
// And inside main code area, you can use these:
// - TRY(another_failable_func()) Try running another failable function, and
//                                return the error if it fails.
// - THROW(error_status)          Return the error
//
// And between FAILABLE_EPILOGUE_BEGIN and FAILABLE_EPILOGUE_END, cleanup
// code comes here. These are executed when the function exits.
// If some cleanup must be done only on failures, you can use DID_FAIL to check if
// it's failure.
// 
// WARNING: Avoid using `return` statement inside failable functions, because that
//          will bypass the cleanup logic. TRY and THROW `goto`s to the
//          cleanup(epilogue) label.

#define FAILABLE_FUNCTION        WARN_UNUSED_RESULT status_t
#define FAILABLE_STATUS_VAR      status
#define FAILABLE_EPILOGUE_LABEL  epilogue

#define FAILABLE_EPILOGUE_BEGIN  FAILABLE_EPILOGUE_LABEL:
#define FAILABLE_EPILOGUE_END    return FAILABLE_STATUS_VAR;
#define FAILABLE_PROLOGUE        status_t FAILABLE_STATUS_VAR = OK;

#define TRY(_x)                 if ((FAILABLE_STATUS_VAR = (_x)) != OK) { goto FAILABLE_EPILOGUE_LABEL; }
#define THROW(_x)               FAILABLE_STATUS_VAR = (_x); goto FAILABLE_EPILOGUE_LABEL;
#define EXIT()                  FAILABLE_STATUS_VAR = OK; goto FAILABLE_EPILOGUE_LABEL;
#define DID_FAIL                (FAILABLE_STATUS_VAR != OK)

