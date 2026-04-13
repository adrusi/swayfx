#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"

struct cmd_results *cmd_vtab_scroll(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "vtab_scroll", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if ((error = checkarg(argc, "vtab_scroll", EXPECTED_AT_MOST, 2))) {
		return error;
	}

	bool up;
	if (strcasecmp(argv[0], "up") == 0) {
		up = true;
	} else if (strcasecmp(argv[0], "down") == 0) {
		up = false;
	} else {
		return cmd_results_new(CMD_FAILURE,
			"Expected 'vtab_scroll up|down [N]'");
	}

	int amount = 1;
	if (argc == 2) {
		char *end;
		amount = (int)strtol(argv[1], &end, 10);
		if (*end || amount < 1) {
			return cmd_results_new(CMD_FAILURE,
				"Expected a positive integer for scroll amount");
		}
	}

	struct sway_container *ctx = config->handler_context.container;
	if (!ctx) {
		return cmd_results_new(CMD_FAILURE,
			"vtab_scroll requires a runtime context (use in a keybinding)");
	}

	// Walk up to the nearest vtabbed ancestor.
	struct sway_container *vtab_con = ctx;
	while (vtab_con && vtab_con->pending.layout != L_VTABBED) {
		vtab_con = vtab_con->pending.parent;
	}
	if (!vtab_con) {
		return cmd_results_new(CMD_FAILURE,
			"Not inside a vtabbed container");
	}

	int n = vtab_con->pending.children ? vtab_con->pending.children->length : 0;
	if (n == 0) {
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (up) {
		vtab_con->vtab_scroll = MAX(0, vtab_con->vtab_scroll - amount);
	} else {
		// Clamp to n-1 here; arrange_children will further clamp to n-max_visible.
		vtab_con->vtab_scroll = MIN(n - 1, vtab_con->vtab_scroll + amount);
	}

	vtab_con->vtab_manual_scroll = true;

	arrange_container(vtab_con);
	transaction_commit_dirty();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
