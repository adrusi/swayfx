#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"

// When enabled, tabbed, stacked and vtabbed containers are framed as one
// object: the tab bar's (or vtab sidebar's) outer edges are drawn in the
// active child's border color and thickness, so the frame runs around the tabs
// and the content together, and the client's border on the tab side stays as a
// divider in the unfocused child_border color.
struct cmd_results *cmd_tab_frame(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "tab_frame", EXPECTED_EQUAL_TO, 1))) {
		return error;
	} else if (strcmp(argv[0], "disable") == 0) {
		config->tab_frame = false;
	} else if (strcmp(argv[0], "enable") == 0) {
		config->tab_frame = true;
	} else {
		return cmd_results_new(CMD_FAILURE,
				"Expected 'tab_frame enable|disable'");
	}

	arrange_root();
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
