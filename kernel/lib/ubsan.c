#include <kernel/arch/interrupts.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/tty.h>
#include <kernel/lib/noreturn.h>
#include <kernel/panic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Ref: https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/ubsan/ubsan_value.h

typedef void *valuehandle_t;

typedef struct sourcelocation sourcelocation_t;
struct sourcelocation {
    char const *filename;
    uint32_t line;
    uint32_t column;
};

typedef struct typedescriptor typedescriptor_t;
struct typedescriptor {
    uint16_t typekind; // See UBSAN_KIND_~ values
    uint16_t typeinfo;
    char typename[1];
};

enum {
    // Integer type:
    //
    // Type info:
    // Bit    0: 1=Signed, 0=Unsigned
    // Bit 1~15: log2(<Bit count>)
    UBSAN_KIND_INTEGER = 0x0000,

    // Floating point
    //
    // Type info:
    // Bit 0~15: Bit width
    UBSAN_KIND_FLOAT   = 0x0001,

    // _BigInt(N)
    //
    // Type info:
    // Bit    0: 1=Signed, 0=Unsigned
    // Bit 1~15: log2(<Bit count>)
    UBSAN_KIND_BIGINT  = 0x0002,

    UBSAN_KIND_UNKNOWN = 0xffff,
};

static void printtypedescriptor(typedescriptor_t const *desc) {
    if (desc == NULL) {
        tty_printf("<no info>");
        return;
    }
    switch(desc->typekind) {
        case UBSAN_KIND_INTEGER:
            tty_printf("(int %c%u) %s", (desc->typeinfo & 1) ? 's' : 'u', 1 << (desc->typeinfo >> 1), desc->typename);
            break;
        case UBSAN_KIND_FLOAT:
            tty_printf("(f%u) %s", desc->typeinfo, desc->typename);
            break;
        case UBSAN_KIND_BIGINT:
            tty_printf("(bigint %c%u) %s", (desc->typeinfo & 1) ? 's' : 'u', 1 << (desc->typeinfo >> 1), desc->typename);
            break;
    }
}

#define DEFINE_RECOVERABLE_ERROR(_name, ...)                                          \
    void __ubsan_handle_##_name( __VA_ARGS__ ) __attribute__((used));                  \
    NORETURN void __ubsan_handle_##_name##_abort( __VA_ARGS__ ) __attribute__((used))

typedef struct typemismatch_data typemismatch_data_t;
struct typemismatch_data {
    sourcelocation_t loc;
    typedescriptor_t *type;
};

DEFINE_RECOVERABLE_ERROR(type_mismatch_v1, typemismatch_data_t *data, valuehandle_t ptr);
static NORETURN void die(void) {
    panic("execution aborted by ubsanitizer\n");
}

static void printheadermessage(void) {
    tty_printf("oops, ubsan detected a kernel UB!\n");
    arch_stacktrace();
}

static void typemismatch(typemismatch_data_t *data, valuehandle_t ptr) {
    bool previnterrupts = arch_interrupts_disable();
    printheadermessage();
    tty_printf("type mismatch error at %s:%d:%d!\n", data->loc.filename, data->loc.line, data->loc.column);
    tty_printf("pointer: %p\n", ptr);
    tty_printf("   type: ");
    printtypedescriptor(data->type);
    tty_printf("\n");
    interrupts_restore(previnterrupts);
}
void __ubsan_handle_type_mismatch_v1(typemismatch_data_t *data, valuehandle_t ptr) {
    typemismatch(data, ptr);
}
NORETURN void __ubsan_handle_type_mismatch_v1_abort(typemismatch_data_t *data, valuehandle_t ptr) {
    typemismatch(data, ptr);
    die();
}

typedef struct ptroverflow_data ptroverflow_data_t;
struct ptroverflow_data {
    sourcelocation_t loc;
};

DEFINE_RECOVERABLE_ERROR(pointer_overflow, ptroverflow_data_t *data, valuehandle_t base, valuehandle_t result);
static void ptroverflow(ptroverflow_data_t *data, valuehandle_t base, valuehandle_t result) {
    bool previnterrupts = arch_interrupts_disable();
    printheadermessage();
    tty_printf("pointer overflow error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    tty_printf("     base pointer: %p\n", base);
    tty_printf("resulting pointer: %p\n", result);
    interrupts_restore(previnterrupts);
}
void __ubsan_handle_pointer_overflow(ptroverflow_data_t *data, valuehandle_t base, valuehandle_t result) {
    ptroverflow(data, base, result);
}
NORETURN void __ubsan_handle_pointer_overflow_abort(ptroverflow_data_t *data, valuehandle_t base, valuehandle_t result) {
    ptroverflow(data, base, result);
    die();
}

typedef struct outofbounds_data outofbounds_data_t;

struct outofbounds_data {
    sourcelocation_t loc;
    typedescriptor_t *array_type;
    typedescriptor_t *index_type;
};

DEFINE_RECOVERABLE_ERROR(out_of_bounds, outofbounds_data_t *data, valuehandle_t index);
static void outofbounds(outofbounds_data_t *data, valuehandle_t index) {
    bool previnterrupts = arch_interrupts_disable();
    printheadermessage();
    tty_printf("out of bounds error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    tty_printf(" array type: ");
    printtypedescriptor(data->array_type);
    tty_printf("\n");
    tty_printf(" index type: ");
    printtypedescriptor(data->index_type);
    tty_printf("\n");
    tty_printf("index value: %zu\n", (size_t)index);
    interrupts_restore(previnterrupts);
}
void __ubsan_handle_out_of_bounds(outofbounds_data_t *data, valuehandle_t index) {
    outofbounds(data, index);
}
NORETURN void __ubsan_handle_out_of_bounds_abort(outofbounds_data_t *data, valuehandle_t index) {
    outofbounds(data, index);
    die();
}

typedef struct shiftoutofbounds_data_t shiftoutofbounds_data_t;
struct shiftoutofbounds_data_t {
    sourcelocation_t loc;
    typedescriptor_t *lhstype;
    typedescriptor_t *rhstype;
};
DEFINE_RECOVERABLE_ERROR(shift_out_of_bounds, shiftoutofbounds_data_t *data, valuehandle_t lhs, valuehandle_t rhs);
static void shiftoutofbounds(shiftoutofbounds_data_t *data, valuehandle_t lhs, valuehandle_t rhs) {
    bool previnterrupts = arch_interrupts_disable();
    printheadermessage();
    tty_printf("shift out of bounds error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    tty_printf("            lhs type: ");
    printtypedescriptor(data->lhstype);
    tty_printf("\n");
    tty_printf("            rhs type: ");
    printtypedescriptor(data->rhstype);
    tty_printf("\n");
    tty_printf("lhs value(as size_t): %zu\n", (size_t)lhs);
    tty_printf("rhs value(as size_t): %zu\n", (size_t)rhs);
    interrupts_restore(previnterrupts);
}
void __ubsan_handle_shift_out_of_bounds(shiftoutofbounds_data_t *data, valuehandle_t lhs, valuehandle_t rhs) {
    shiftoutofbounds(data, lhs, rhs);
}
NORETURN void __ubsan_handle_shift_out_of_bounds_abort(shiftoutofbounds_data_t *data, valuehandle_t lhs, valuehandle_t rhs) {
    shiftoutofbounds(data, lhs, rhs);
    die();
}

typedef struct invalidvalue_data invalidvalue_data_t;
struct invalidvalue_data {
    sourcelocation_t loc;
    typedescriptor_t *type;
};
DEFINE_RECOVERABLE_ERROR(load_invalid_value, invalidvalue_data_t *data, valuehandle_t val);
static void loadinvalidvalue(invalidvalue_data_t *data, valuehandle_t val) {
    bool previnterrupts = arch_interrupts_disable();
    printheadermessage();
    tty_printf("load invalid value error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    tty_printf("           type: ");
    printtypedescriptor(data->type);
    tty_printf("\n");
    tty_printf("value(as size_t): %zu\n", (size_t)val);
    interrupts_restore(previnterrupts);
}
void __ubsan_handle_load_invalid_value(invalidvalue_data_t *data, valuehandle_t val) {
    loadinvalidvalue(data, val);
}
NORETURN void __ubsan_handle_load_invalid_value_abort(invalidvalue_data_t *data, valuehandle_t val) {
    loadinvalidvalue(data, val);
    die();
}


typedef struct overflow_data overflow_data_t;
struct overflow_data {
    sourcelocation_t loc;
    typedescriptor_t *type;
};

DEFINE_RECOVERABLE_ERROR(add_overflow, overflow_data_t *data, valuehandle_t lhs, valuehandle_t rhs);
static void overflow(char const *type, overflow_data_t *data, valuehandle_t lhs, valuehandle_t rhs) {
    bool previnterrupts = arch_interrupts_disable();
    printheadermessage();
    tty_printf("%s overflow error at %s:%d:%d!\n", type, data->loc.filename, data->loc.column, data->loc.line);
    tty_printf("                type: ");
    printtypedescriptor(data->type);
    tty_printf("\n");
    tty_printf("lhs value(as size_t): %zu\n", (size_t)lhs);
    tty_printf("rhs value(as size_t): %zu\n", (size_t)rhs);
    interrupts_restore(previnterrupts);
    ////////////////////////////////////////////////////////////////////////////
}

#define DEFINE_OVERFLOW_ERROR(type, name)                                                                                \
    DEFINE_RECOVERABLE_ERROR(type##_overflow, overflow_data_t *data, valuehandle_t lhs, valuehandle_t rhs);              \
    void __ubsan_handle_##type##_overflow(overflow_data_t *data, valuehandle_t lhs, valuehandle_t rhs) {                 \
        overflow(name, data, lhs, rhs);                                                                                  \
    }                                                                                                                    \
    NORETURN void __ubsan_handle_handle_##type##_overflow(overflow_data_t *data, valuehandle_t lhs, valuehandle_t rhs) { \
        overflow(name, data, lhs, rhs);                                                                                  \
        die();                                                                                                           \
    }

DEFINE_OVERFLOW_ERROR(add, "add")
DEFINE_OVERFLOW_ERROR(sub, "sub")
DEFINE_OVERFLOW_ERROR(mul, "mul")
DEFINE_OVERFLOW_ERROR(negate, "negate")
DEFINE_OVERFLOW_ERROR(divrem, "remainder")



