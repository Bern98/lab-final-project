#include "errmsg.h"

void die_if_error(char *msg, int ret)
{
    if (ret < 0)
    {
        perror(msg);
        exit(1);
    }
}