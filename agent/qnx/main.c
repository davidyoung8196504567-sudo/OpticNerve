/* main.c: the `opticnerve` binary. One program, subcommands (private design, section 4.10). */
#include <stdio.h>
#include <string.h>

int serve_main(int argc, char **argv);
int cli_main(const char *cmd, int argc, char **argv);

static const char usage[] =
	"usage: opticnerve serve [-display=N] [-port=P] [-passfile=PATH] [-holdoff=MS] [-logintimeout=S] [-v]\n"
	"                        [-foreground]                                                             (root)\n"
	"       opticnerve status\n"
	"       opticnerve wait [-timeout=S] [-since=COUNT] [-quiet=MS]\n"
	"       opticnerve current\n"
	"       opticnerve capture X Y W H FILE\n"
	"       opticnerve stop                                                                            (root)\n"
	"       opticnerve passwd [-passfile=PATH] < password                                              (root)\n";

int main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "serve") == 0)
		return serve_main(argc - 2, argv + 2);
	if (argc >= 2) {
		int rc = cli_main(argv[1], argc - 2, argv + 2);
		if (rc >= 0)
			return rc;
	}
	fputs(usage, stderr);
	return 2; /* no subcommand, or an unknown one, starts nothing */
}
