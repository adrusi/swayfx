#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"

struct cmd_results *cmd_vtab_width(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "vtab_width", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	char *inv;
	int value = strtol(argv[0], &inv, 10);
	if (*inv != '\0' || value < 0) {
		return cmd_results_new(CMD_FAILURE, "Invalid width specified");
	}

	config->vtab_width = value;

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		arrange_workspace(output_get_active_workspace(output));
	}
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
