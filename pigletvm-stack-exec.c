#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pigletvm-stack.h"

#define MAX_CODE_LEN 4096
#define MAX_LINE_LEN 256

static char *error_to_msg[] = {
    [SUCCESS] = "success",
    [ERROR_DIVISION_BY_ZERO] = "division by zero",
    [ERROR_UNKNOWN_OPCODE] = "unknown opcode",
    [ERROR_END_OF_STREAM] = "end of stream",
};

static struct opcode_to_disinfo {
    size_t num_args;
    char *name;
} opcode_to_disinfo[] = {
    [OP_ABORT] = {0, "ABORT"},
    [OP_PUSHI] = {1, "PUSHI"},
    [OP_ADD] = {0, "ADD"},
    [OP_SUB] = {0, "SUB"},
    [OP_DIV] = {0, "DIV"},
    [OP_MUL] = {0, "MUL"},
    [OP_POP_RES] = {0, "POP_RES"},
    [OP_DONE] = {0, "DONE"},
};

static void opname_to_opcode(const char *opname, int *op, size_t *num_args)
{
    for (int i = 0; i < OP_NUMBER_OF_OPS; i++) {
        if (strcasecmp(opcode_to_disinfo[i].name, opname) == 0) {
            *op = i;
            *num_args = opcode_to_disinfo[i].num_args;
            return;
        }
    }

    fprintf(stderr, "Unknown operation name: %s\n", opname);
    exit(EXIT_FAILURE);
}

static void print_arg(char *name, size_t arg_offset, uint8_t *bytecode, size_t num_args)
{
    printf("%s", name);
    for (size_t arg_i = 0; arg_i < num_args; arg_i++ )
        printf(" %d", bytecode[arg_offset++]);
    printf("\n");
}

static size_t print_instruction(uint8_t *bytecode, size_t offset)
{
    uint8_t op = bytecode[offset++];
    char *op_name = opcode_to_disinfo[op].name;
    size_t num_args = opcode_to_disinfo[op].num_args;
    print_arg(op_name, offset, bytecode, num_args);
    return offset + num_args;
}

static int disassemble(uint8_t *bytecode)
{
    size_t offset = 0;
    while (bytecode[offset])
        offset = print_instruction(bytecode, offset);
    return EXIT_SUCCESS;
}

static int run(uint8_t *bytecode)
{
    interpret_result res = vm_interpret(bytecode);
    if (res != SUCCESS) {
        fprintf(stderr, "Runtime error: %s\n", error_to_msg[res]);
        return EXIT_FAILURE;
    }
    uint64_t result_value = vm_get_result();
    printf("Result value: %" PRIu64 "\n", result_value);
    return EXIT_SUCCESS;
}

static size_t compile_line(char *line, uint8_t *bytecode, size_t pc)
{
    /* Ignore comments and empty lines*/
    if (line[0] == '#' || line[0] == '\n')
        return pc;

    char *saveptr = NULL;
    char *opname = strtok_r(line, " ", &saveptr);
    if (!opname) {
        fprintf(stderr, "Cannot parse string: %s\n", line);
        exit(EXIT_FAILURE);
    }

    /* Strip opname, find it's info, put it into bytecode */
    int op;
    size_t arg_num;
    char opname_stripped[MAX_LINE_LEN];
    char *d = opname_stripped;

    /* Strip spaces */
    do
        while(isspace(*opname))
            opname++;
    while((*d++ = *opname++));
    opname_to_opcode(opname_stripped, &op, &arg_num);
    bytecode[pc++] = op;

    /* See if there any immediate args left on the line to be put into bytecode */
    for (;;) {
        char *arg = strtok_r(NULL, " ", &saveptr);
        if (!arg && arg_num) {
            fprintf(stderr, "Not enough arguments supplied: %s\n", line);
            exit(EXIT_FAILURE);
        } else if (arg && !arg_num) {
            fprintf(stderr, "Too many arguments supplied: %s\n", line);
            exit(EXIT_FAILURE);
        } else if (!arg && !arg_num) {
            /* This is fine */
            break;
        }

        int8_t arg_val = 0;
        if (sscanf(arg, "%" SCNi8, &arg_val) != 1) {
            fprintf(stderr, "Invalid argument supplied: %s\n", arg);
            exit(EXIT_FAILURE);
        }

        bytecode[pc++] = (uint8_t)arg_val;
        arg_num--;
    }

    return pc;
}

static uint8_t *compile_file(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "File does not exist: %s\n", path);
        exit(EXIT_FAILURE);
    }

    uint8_t *bytecode = malloc(MAX_CODE_LEN);
    if (!bytecode) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(EXIT_FAILURE);
    }

    size_t pc = 0;
    char line_buf[MAX_LINE_LEN];
    while (fgets(line_buf, MAX_LINE_LEN, file))
        pc = compile_line(line_buf, bytecode, pc);

    fclose(file);
    return bytecode;
}

static uint8_t *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "File does not exist: %s\n", path);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = (size_t)ftell(file);
    rewind(file);

    uint8_t *buf = malloc(file_size + 1);
    if (!buf) {
        fprintf(stderr, "Memory allocation failure: %s\n", path);
        exit(EXIT_FAILURE);
    }

    size_t bytes_read = fread(buf, sizeof(uint8_t), file_size, file);
    if (bytes_read < file_size) {
        fprintf(stderr, "Failed to read: %s\n", path);
        exit(EXIT_FAILURE);
    }

    buf[bytes_read] = '\0';
    fclose(file);
    return buf;
}

static void write_file(const uint8_t *bytecode, const char *path)
{
    FILE *file = fopen(path, "wb");
    size_t bytecode_len = strlen((char *)bytecode);
    if (fwrite(bytecode, bytecode_len, 1, file) != 1) {
        fprintf(stderr, "Failed to write to a file: %s\n", path);
        exit(EXIT_FAILURE);
    }
    fclose(file);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: <dis|run|asm> <path/to/bytecode> [<path/to/output>]\n");
        exit(EXIT_FAILURE);
    }

    const char *cmd = argv[1];

    int res;
    if (0 == strcmp(cmd, "dis")) {
        if (argc != 3) {
            fprintf(stderr, "Usage: dis <path/to/bytecode>\n");
            exit(EXIT_FAILURE);
        }

        const char *path = argv[2];
        uint8_t *bytecode = read_file(path);

        res = disassemble(bytecode);

        free(bytecode);
    } else if (0 == strcmp(cmd, "run")) {
        if (argc != 3) {
            fprintf(stderr, "Usage: run <path/to/bytecode>\n");
            exit(EXIT_FAILURE);
        }

        const char *path = argv[2];
        uint8_t *bytecode = read_file(path);

        res = run(bytecode);

        free(bytecode);
    } else if (0 == strcmp(cmd, "asm")) {
        if (argc != 4) {
            fprintf(stderr, "Usage: asm <path/to/asm> <path/to/output/bytecode>\n");
            exit(EXIT_FAILURE);
        }

        const char *input_path = argv[2];
        const char *output_path = argv[3];

        uint8_t *bytecode = compile_file(input_path);
        write_file(bytecode, output_path);

        res = EXIT_SUCCESS;
        free(bytecode);
    } else {
        fprintf(stderr, "Unknown cmd: %s\n", cmd);;
        res = EXIT_FAILURE;
    }

    return res;
}