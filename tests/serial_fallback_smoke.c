#include "bbasic_runtime.h"

int main(void)
{
    if (bb_file_open("com2:9600,N,8,1", 1.0, 4.0, 2048.0) != 0) return 2;
    if (bb_file_loc(1.0) != 0.0) return 3;
    bb_file_print_string(1.0, "test");
    bb_set_com(1.0, "2400,N,8,1");
    bb_file_close(1.0);
    return 0;
}
