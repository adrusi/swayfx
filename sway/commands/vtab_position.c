#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"

struct cmd_results *cmd_vtab_position(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "vtab_position", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "left") == 0) {
		config->vtab_left = true;
	} else if (strcasecmp(argv[0], "right") == 0) {
		config->vtab_left = false;
	} else {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'vtab_position left|right'");
	}

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		arrange_workspace(output_get_active_workspace(output));
	}
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
