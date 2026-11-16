/* main.c: the `opticnerve` binary. One program, subcommands (private design, section 4.10). */
#include <stdio.h>
#include <string.h>

int serve_main(int argc, char **argv);

int main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "serve") == 0)
		return serve_main(argc - 2, argv + 2);
	fprintf(stderr, "usage: opticnerve serve [-display=N] [-port=P] [-passfile=PATH] [-holdoff=MS]\n");
	return 2; /* no subcommand starts nothing */
}
