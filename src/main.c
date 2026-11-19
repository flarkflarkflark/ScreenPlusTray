/* Entry point for screenplus-tray. */
#include "client.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) {
            SpState st;
            sp_state_init(&st);
            sp_client_fetch_full(&st);
            sp_state_dump(&st);
            return 0;
        }
    }

    puts("screenplus-tray: use --dump in step 2.");
    return 0;
}
