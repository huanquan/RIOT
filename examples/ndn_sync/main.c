/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       NDN ping application
 *
 * @author      Wentao Shang <wentaoshang@gmaiil.com>
 *
 * @}
 */

#include <stdio.h>

#include "net/ndn/ndn.h"
#include "shell.h"
#include "msg.h"
#include "config.h"

extern int vsync(int argc, char **argv);

// static const shell_command_t shell_commands[] = {
//     { "vsync", "VectorSync", vsync },
//     { NULL, NULL, NULL }
// };

int main(void)
{
    config_load();
    printf("Node id=%d\n", sysconfig.id);

    // /* start shell */
    // puts("All up, running the shell now");
    // char line_buf[SHELL_DEFAULT_BUFSIZE];
    // shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);
    char* argv[1] = { "vsync" };
    vsync(1, argv);

    /* should be never reached */
    return 0;
}
