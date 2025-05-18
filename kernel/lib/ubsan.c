#include <kernel/arch/interrupts.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/panic.h>
#include <stddef.h>
#include <stdint.h>

/* Ref: https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/ubsan/ubsan_value.h */

struct source_location {
    char const *filename;
    uint32_t line;
    uint32_t column;
};

struct type_descriptor {
    uint16_t typekind; /* See UBSAN_KIND_~ values */
    uint16_t typeinfo;
    char typename[1];
};

/*
 * Integer type:
 *
 * Type info:
 *   Bit    0: 1=Signed, 0=Unsigned
 *   Bit 1~15: log2(<Bit count>)
 */
#define UBSAN_KIND_INTEGER 0x0000

/*
 * Floating point
 *
 * Type info:
 *   Bit 0~15: Bit width
*/
#define UBSAN_KIND_FLOAT 0x0001

/*
 * _BigInt(N)
 *
 * Type info:
 *   Bit    0: 1=Signed, 0=Unsigned
 *   Bit 1~15: log2(<Bit count>)
*/
#define UBSAN_KIND_BIGINT 0x0002


#define UBSAN_KIND_UNKNOWN 0xffff

static void print_type_descriptor(struct type_descriptor const *desc) {
    if (desc == NULL) {
        Co_Printf("<no info>");
        return;
    }
    switch (desc->typekind) {
    case UBSAN_KIND_INTEGER:
        Co_Printf("(int %c%u) %s", (desc->typeinfo & 1U) ? 's' : 'u', 1U << ((uint32_t)desc->typeinfo >> 1), desc->typename);
        break;
    case UBSAN_KIND_FLOAT:
        Co_Printf("(f%u) %s", desc->typeinfo, desc->typename);
        break;
    case UBSAN_KIND_BIGINT:
        Co_Printf("(bigint %c%u) %s", (desc->typeinfo & 1U) ? 's' : 'u', 1U << ((uint32_t)desc->typeinfo >> 1), desc->typename);
        break;
    default:
        Co_Printf("??");
    }
}

#define DEFINE_RECOVERABLE_ERROR(_name, ...)                        \
    void __ubsan_handle_##_name(__VA_ARGS__) __attribute__((used)); \
    [[noreturn]] void __ubsan_handle_##_name##_abort(__VA_ARGS__) __attribute__((used))

struct type_mismatch_data {
    struct source_location loc;
    struct type_descriptor *type;
};

DEFINE_RECOVERABLE_ERROR(type_mismatch_v1, struct type_mismatch_data *data, void *ptr);
[[noreturn]] static void die(void) {
    Panic("execution aborted by ubsanitizer\n");
}

static void printheadermessage(void) {
    Co_Printf("oops, ubsan detected a kernel UB!\n");
    Arch_Stacktrace();
}

static void type_mismatch(struct type_mismatch_data *data, void *ptr) {
    bool prev_interrupts = Arch_Irq_Disable();
    printheadermessage();
    Co_Printf("type mismatch error at %s:%d:%d!\n", data->loc.filename, data->loc.line, data->loc.column);
    Co_Printf("pointer: %p\n", ptr);
    Co_Printf("   type: ");
    print_type_descriptor(data->type);
    Co_Printf("\n");
    Arch_Irq_Restore(prev_interrupts);
}
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data *data, void *ptr) {
    type_mismatch(data, ptr);
}
[[noreturn]] void __ubsan_handle_type_mismatch_v1_abort(struct type_mismatch_data *data, void *ptr) {
    type_mismatch(data, ptr);
    die();
}

struct pointer_overflow_data {
    struct source_location loc;
};

DEFINE_RECOVERABLE_ERROR(pointer_overflow, struct pointer_overflow_data *data, void *base, void *result);
static void pointer_overflow(struct pointer_overflow_data *data, void *base, void *result) {
    bool prev_interrupts = Arch_Irq_Disable();
    printheadermessage();
    Co_Printf("pointer overflow error at %s:%d:%d!\n", data->loc.filename, data->loc.line, data->loc.column);
    Co_Printf("     base pointer: %p\n", base);
    Co_Printf("resulting pointer: %p\n", result);
    Arch_Irq_Restore(prev_interrupts);
}
void __ubsan_handle_pointer_overflow(struct pointer_overflow_data *data, void *base, void *result) {
    pointer_overflow(data, base, result);
}
[[noreturn]] void __ubsan_handle_pointer_overflow_abort(struct pointer_overflow_data *data, void *base, void *result) {
    pointer_overflow(data, base, result);
    die();
}

struct out_of_bounds_data {
    struct source_location loc;
    struct type_descriptor *array_type;
    struct type_descriptor *index_type;
};

DEFINE_RECOVERABLE_ERROR(out_of_bounds, struct out_of_bounds_data *data, void *index);
static void out_of_bounds(struct out_of_bounds_data *data, void *index) {
    bool prev_interrupts = Arch_Irq_Disable();
    printheadermessage();
    Co_Printf("out of bounds error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    Co_Printf(" array type: ");
    print_type_descriptor(data->array_type);
    Co_Printf("\n");
    Co_Printf(" index type: ");
    print_type_descriptor(data->index_type);
    Co_Printf("\n");
    Co_Printf("index value: %zu\n", (size_t)index);
    Arch_Irq_Restore(prev_interrupts);
}
void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data, void *index) {
    out_of_bounds(data, index);
}
[[noreturn]] void __ubsan_handle_out_of_bounds_abort(struct out_of_bounds_data *data, void *index) {
    out_of_bounds(data, index);
    die();
}

struct shift_out_of_bounds_data {
    struct source_location loc;
    struct type_descriptor *lhstype;
    struct type_descriptor *rhstype;
};
DEFINE_RECOVERABLE_ERROR(shift_out_of_bounds, struct shift_out_of_bounds_data *data, void *lhs, void *rhs);
static void shift_out_of_bounds(struct shift_out_of_bounds_data *data, void *lhs, void *rhs) {
    bool prev_interrupts = Arch_Irq_Disable();
    printheadermessage();
    Co_Printf("shift out of bounds error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    Co_Printf("            lhs type: ");
    print_type_descriptor(data->lhstype);
    Co_Printf("\n");
    Co_Printf("            rhs type: ");
    print_type_descriptor(data->rhstype);
    Co_Printf("\n");
    Co_Printf("lhs value(as size_t): %zu\n", (size_t)lhs);
    Co_Printf("rhs value(as size_t): %zu\n", (size_t)rhs);
    Arch_Irq_Restore(prev_interrupts);
}
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data, void *lhs, void *rhs) {
    shift_out_of_bounds(data, lhs, rhs);
}
[[noreturn]] void __ubsan_handle_shift_out_of_bounds_abort(struct shift_out_of_bounds_data *data, void *lhs, void *rhs) {
    shift_out_of_bounds(data, lhs, rhs);
    die();
}

struct invalid_value_data {
    struct source_location loc;
    struct type_descriptor *type;
};
DEFINE_RECOVERABLE_ERROR(load_invalid_value, struct invalid_value_data *data, void *val);
static void load_invalid_value(struct invalid_value_data *data, void *val) {
    bool prev_interrupts = Arch_Irq_Disable();
    printheadermessage();
    Co_Printf("load invalid value error at %s:%d:%d!\n", data->loc.filename, data->loc.column, data->loc.line);
    Co_Printf("           type: ");
    print_type_descriptor(data->type);
    Co_Printf("\n");
    Co_Printf("value(as size_t): %zu\n", (size_t)val);
    Arch_Irq_Restore(prev_interrupts);
}
void __ubsan_handle_load_invalid_value(struct invalid_value_data *data, void *val) {
    load_invalid_value(data, val);
}
[[noreturn]] void __ubsan_handle_load_invalid_value_abort(struct invalid_value_data *data, void *val) {
    load_invalid_value(data, val);
    die();
}

struct overflow_data {
    struct source_location loc;
    struct type_descriptor *type;
};

DEFINE_RECOVERABLE_ERROR(add_overflow, struct overflow_data *data, void *lhs, void *rhs);
static void overflow(char const *type, struct overflow_data *data, void *lhs, void *rhs) {
    bool prev_interrupts = Arch_Irq_Disable();
    printheadermessage();
    Co_Printf("%s overflow error at %s:%d:%d!\n", type, data->loc.filename, data->loc.column, data->loc.line);
    Co_Printf("                type: ");
    print_type_descriptor(data->type);
    Co_Printf("\n");
    Co_Printf("lhs value(as size_t): %zu\n", (size_t)lhs);
    Co_Printf("rhs value(as size_t): %zu\n", (size_t)rhs);
    Arch_Irq_Restore(prev_interrupts);
}

#define DEFINE_OVERFLOW_ERROR(type, name)                                                                         \
    DEFINE_RECOVERABLE_ERROR(type##_overflow, struct overflow_data *data, void *lhs, void *rhs);                  \
    void __ubsan_handle_##type##_overflow(struct overflow_data *data, void *lhs, void *rhs) {                     \
        overflow(name, data, lhs, rhs);                                                                           \
    }                                                                                                             \
    [[noreturn]] void __ubsan_handle_handle_##type##_overflow(struct overflow_data *data, void *lhs, void *rhs) { \
        overflow(name, data, lhs, rhs);                                                                           \
        die();                                                                                                    \
    }

DEFINE_OVERFLOW_ERROR(add, "add")
DEFINE_OVERFLOW_ERROR(sub, "sub")
DEFINE_OVERFLOW_ERROR(mul, "mul")
DEFINE_OVERFLOW_ERROR(negate, "negate")
DEFINE_OVERFLOW_ERROR(divrem, "remainder")
