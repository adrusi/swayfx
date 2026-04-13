#include <stdlib.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"

struct cmd_results *cmd_vtab_padding(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "vtab_padding", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	char *end;
	int value = (int)strtol(argv[0], &end, 10);
	if (*end != '\0' || value < 0) {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'vtab_padding <px>' with a non-negative integer");
	}

	config->vtab_padding = value;
	arrange_root();
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
