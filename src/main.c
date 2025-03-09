#include <stdio.h>
#include <stdlib.h>

void print_usage(FILE *stream) {
    fprintf(stream, "Usage: ./excel-cli <input.csv>\n");
}

int main(int argc, char **argv) {
    if(argc < 2) {
        print_usage(stderr);
        fprintf(stderr, "ERROR: input file is not provided\n");
        exit(1);
    }

    const char *input_file_path = argv[1];
    

    return 0;
}
