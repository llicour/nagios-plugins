/******************************************************************************
 *
 * CHECK_NAGIOS.C
 *
 * Program: Nagios process plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * $Id$
 *
 * License Information:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include "common.h"
#include "popen.h"
#include "utils.h"

#define PROGNAME "check_nagios"

int process_arguments (int, char **);
void print_usage (void);
void print_help (void);

char *status_log = NULL;
char *process_string = NULL;
int expire_minutes = 0;

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	char input_buffer[MAX_INPUT_BUFFER];
	unsigned long latest_entry_time = 0L;
	unsigned long temp_entry_time = 0L;
	int proc_entries = 0;
	time_t current_time;
	char *temp_ptr;
	FILE *fp;

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		printf ("Cannot catch SIGALRM");
		return STATE_UNKNOWN;
	}

	/* handle timeouts gracefully... */
	alarm (timeout_interval);

	/* open the status log */
	fp = fopen (status_log, "r");
	if (fp == NULL) {
		printf ("Error: Cannot open status log for reading!\n");
		return STATE_CRITICAL;
	}

	/* get the date/time of the last item updated in the log */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		temp_ptr = strtok (input_buffer, "]");
		temp_entry_time =
			(temp_ptr == NULL) ? 0L : strtoul (temp_ptr + 1, NULL, 10);
		if (temp_entry_time > latest_entry_time)
			latest_entry_time = temp_entry_time;
	}
	fclose (fp);

	/* run the command to check for the Nagios process.. */
	child_process = spopen (PS_RAW_COMMAND);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", PS_RAW_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", PS_RAW_COMMAND);
	}

	/* cound the number of matching Nagios processes... */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		if (strstr (input_buffer, process_string))
			proc_entries++;
	}

	/* If we get anything on stderr, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	/* reset the alarm handler */
	alarm (0);

	if (proc_entries == 0) {
		printf ("Could not locate a running Nagios process!\n");
		return STATE_CRITICAL;
	}

	result = STATE_OK;

	time (&current_time);
	if ((current_time - latest_entry_time) > (expire_minutes * 60))
		result = STATE_WARNING;

	printf
		("Nagios %s: located %d process%s, status log updated %d second%s ago\n",
		 (result == STATE_OK) ? "ok" : "problem", proc_entries,
		 (proc_entries == 1) ? "" : "es",
		 (int) (current_time - latest_entry_time),
		 ((int) (current_time - latest_entry_time) == 1) ? "" : "s");

	return result;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"filename", required_argument, 0, 'F'},
		{"expires", required_argument, 0, 'e'},
		{"command", required_argument, 0, 'C'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	if (argc < 2)
		return ERROR;

	if (!is_option (argv[1])) {
		status_log = argv[1];
		if (is_intnonneg (argv[2]))
			expire_minutes = atoi (argv[2]);
		else
			terminate (STATE_UNKNOWN,
								 "Expiration time must be an integer (seconds)\nType '%s -h' for additional help\n",
								 PROGNAME);
		process_string = argv[3];
		return OK;
	}

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, "+hVF:C:e:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+hVF:C:e:");
#endif

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf ("%s: Unknown argument: %c\n\n", my_basename (argv[0]), optopt);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 'F':									/* hostname */
			status_log = optarg;
			break;
		case 'C':									/* hostname */
			process_string = optarg;
			break;
		case 'e':									/* hostname */
			if (is_intnonneg (optarg))
				expire_minutes = atoi (optarg);
			else
				terminate (STATE_UNKNOWN,
									 "Expiration time must be an integer (seconds)\nType '%s -h' for additional help\n",
									 PROGNAME);
			break;
		}
	}


	if (status_log == NULL)
		terminate (STATE_UNKNOWN,
							 "You must provide the status_log\nType '%s -h' for additional help\n",
							 PROGNAME);
	else if (process_string == NULL)
		terminate (STATE_UNKNOWN,
							 "You must provide a process string\nType '%s -h' for additional help\n",
							 PROGNAME);

	return OK;
}





void
print_usage (void)
{
	printf
		("Usage: %s -F <status log file> -e <expire_minutes> -C <process_string>\n",
		 PROGNAME);
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 2000 Ethan Galstad/Karl DeBisschop\n\n"
		 "This plugin attempts to check the status of the Nagios process on the local\n"
		 "machine. The plugin will check to make sure the Nagios status log is no older\n"
		 "than the number of minutes specified by the <expire_minutes> option.  It also\n"
		 "uses the /bin/ps command to check for a process matching whatever you specify\n"
		 "by the <process_string> argument.\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 "-F, --filename=FILE\n"
		 "   Name of the log file to check\n"
		 "-e, --expires=INTEGER\n"
		 "   Seconds aging afterwhich logfile is condsidered stale\n"
		 "-C, --command=STRING\n"
		 "   Command to search for in process table\n"
		 "-h, --help\n"
		 "   Print this help screen\n"
		 "-V, --version\n"
		 "   Print version information\n\n"
		 "Example:\n"
		 "   ./check_nagios -H /usr/local/nagios/var/status.log -e 5 -C /usr/local/nagios/bin/nagios\n");
}
