#include <stdlib.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"

static bool parse_px(const char *s, int *out) {
	char *end;
	int value = (int)strtol(s, &end, 10);
	if (*end != '\0' || value < 0) {
		return false;
	}
	*out = value;
	return true;
}

// Width of the selection rule drawn on the active tab of tabbed, stacked and
// vtabbed containers: along the outer edge of vertical tabs and the top edge of
// horizontal tabs. Uses the tab's border color. 0 disables it.
//   titlebar_indicator <vertical> [<horizontal>]
struct cmd_results *cmd_titlebar_indicator(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "titlebar_indicator", EXPECTED_AT_LEAST, 1)) ||
			(error = checkarg(argc, "titlebar_indicator", EXPECTED_AT_MOST, 2))) {
		return error;
	}

	int vertical, horizontal;
	if (!parse_px(argv[0], &vertical) ||
			!parse_px(argc > 1 ? argv[1] : argv[0], &horizontal)) {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'titlebar_indicator <vertical> [<horizontal>]' "
			"with non-negative integers");
	}

	config->titlebar_indicator = vertical;
	config->titlebar_indicator_horizontal = horizontal;
	arrange_root();
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
