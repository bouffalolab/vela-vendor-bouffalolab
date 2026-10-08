/****************************************************************************
 * vendor/bouffalolab/apps/wireless_tests/mible/mible_main.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/* Interactive shell for the zblue mible test commands.  The zblue shell
 * only knows the root commands listed in its port/sections/defines.c, and
 * mible is not among them, so this command runs the mible subcommands
 * itself:
 *
 *   nsh> mible
 *   mible> init
 *   mible> peripheral on
 *   mible> log_show 5
 *
 * zblue threads are pthreads of the task that runs z_sys_init(), so the
 * stack runs only while this shell runs; after "q" reboot before using
 * Bluetooth again.
 *
 * Without BT_SHELL the zblue shell (port/subsys/shell/shell.c) is not
 * built, so the output functions and ctx_shell that mible_test.c uses
 * are defined here.  Like that shell they print every level at LOG_INFO,
 * which also avoids LOG_ERR: zblue's logging/log.h redefines it.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <zephyr/shell/shell.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MIBLE_MAX_ARGS  16

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

extern void z_sys_init(void);

/* Root entry registered by SHELL_CMD_ARG_REGISTER(mible, ...) */

extern const union shell_cmd_entry shell_cmd_mible;

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef CONFIG_BT_SHELL
/* Shell of the running command; declared in host/shell/bt.h */

const struct shell *ctx_shell;
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* mible_test.c keeps the shell pointer in ctx_shell for its callbacks, so
 * the shell must outlive main().
 */

static struct shell_ctx g_mible_ctx;
static struct shell g_mible_shell =
{
  .ctx = &g_mible_ctx
};

/* z_sys_init() starts the zblue work queues; it must run only once. */

static bool g_mible_started;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void mible_help(void)
{
  const struct shell_static_entry *cmd;

  printf("mible subcommands (q to quit):\n");
  for (cmd = shell_cmd_mible.entry->subcmd->entry; cmd->syntax; cmd++)
    {
      printf("  %s %s\n", cmd->syntax, cmd->help ? cmd->help : "");
    }
}

static void mible_execute(const struct shell *sh, int argc, char **argv)
{
  const struct shell_static_entry *cmd;

  for (cmd = shell_cmd_mible.entry->subcmd->entry; cmd->syntax; cmd++)
    {
      if (strcmp(argv[0], cmd->syntax) != 0)
        {
          continue;
        }

      /* mandatory counts the subcommand itself */

      if (argc < cmd->args.mandatory ||
          argc > cmd->args.mandatory + cmd->args.optional)
        {
          printf("usage: %s %s\n", cmd->syntax,
                 cmd->help ? cmd->help : "");
          return;
        }

      memcpy(&sh->ctx->active_cmd, cmd, sizeof(*cmd));
      cmd->handler(sh, argc, argv);
      return;
    }

  printf("unknown command: %s\n", argv[0]);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifndef CONFIG_BT_SHELL
void shell_fprintf_info(const struct shell *sh, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void shell_fprintf_normal(const struct shell *sh, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void shell_fprintf_warn(const struct shell *sh, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void shell_fprintf_error(const struct shell *sh, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

/* Usage of the subcommand being run; handlers call it on bad arguments */

void shell_help(const struct shell *sh)
{
  const struct shell_static_entry *cmd = &sh->ctx->active_cmd;

  syslog(LOG_INFO, "usage: %s %s\n", cmd->syntax,
         cmd->help ? cmd->help : "");
}
#endif

int main(int argc, FAR char *argv[])
{
  FAR char *line = NULL;
  size_t size = 0;

  if (g_mible_started)
    {
      printf("mible already ran since boot; reboot to use it again\n");
      return EXIT_FAILURE;
    }

  g_mible_started = true;
  z_sys_init();

  for (; ; )
    {
      FAR char *args[MIBLE_MAX_ARGS];
      FAR char *saveptr = NULL;
      FAR char *token;
      ssize_t len;
      int nargs = 0;

      printf("mible> ");
      fflush(stdout);

      len = getline(&line, &size, stdin);
      if (len < 0)
        {
          break;
        }

      for (token = strtok_r(line, " \t\r\n", &saveptr);
           token != NULL && nargs < MIBLE_MAX_ARGS;
           token = strtok_r(NULL, " \t\r\n", &saveptr))
        {
          args[nargs++] = token;
        }

      if (nargs == 0)
        {
          continue;
        }

      if (strcmp(args[0], "q") == 0)
        {
          break;
        }

      if (strcmp(args[0], "help") == 0)
        {
          mible_help();
          continue;
        }

      mible_execute(&g_mible_shell, nargs, args);
    }

  free(line);
  return 0;
}
