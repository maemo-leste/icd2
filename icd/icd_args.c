#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <icd_log.h>
#include "icd_context.h"
#include "icd_exec.h"
#include "icd_args.h"

#include "config.h"

static struct option const long_options[] =
{
  {"deamon",   no_argument,       0, 'd'},
  {"help",     no_argument,       0, 'h'},
  {"version",  no_argument,       0, 'V'},
  {"loglevel", required_argument, 0, 'l'},
  {NULL, 0, NULL, 0}
};

/**
 * Print usage information
 *
 * @param program_name  name of the executable
 * @param status        exit with status
 */
static void
icd_args_usage(const char *program_name, int status)
{
  printf("%s - " PACKAGE_STRING "\n", program_name);
  printf(
    "Usage: %s [OPTION]...\n"
    "Options:\n"
    "-h, --help\t\tDisplay this help and exit\n"
    "-V, --version\t\tOutput version information and exit\n"
    "-d, --daemon\t\tSend process to the background\n"
    "-l, --loglevel\t\tSet logging level (0 (debug) gives all logs, 1 (info) is default)\n",
    program_name);

  exit(status);
}

/**
 * Process commandline options.
 *
 * @param  argc     Parameter given to #main()
 * @param  argv     Parameter given to #main()
 * @param  context  ICd context
 *
 * @return Index of first non-option argument
 */
gint
icd_args_decode(int argc, char *argv[], struct icd_context *context)
{
  while (1)
  {
    int opt = getopt_long(argc, argv, "hVdl:", long_options, 0);

    if (opt == -1)
      break;

    switch (opt)
    {
      case 'V':
        puts(PACKAGE_STRING);
        exit(0);
      case 'd':
        context->daemon = TRUE;
        break;
      case 'h':
        icd_args_usage(*argv, 0);
      case 'l':
        icd_log_set_level(strtol(optarg, 0, 10));
        break;
      default:
        icd_args_usage(*argv, 1);
    }
  }

  return optind;
}
