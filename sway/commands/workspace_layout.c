#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/container.h"

struct cmd_results *cmd_workspace_layout(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "workspace_layout", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	if (strcasecmp(argv[0], "default") == 0) {
		config->default_layout = L_NONE;
	} else if (strcasecmp(argv[0], "stacking") == 0) {
		config->default_layout = L_STACKED;
	} else if (strcasecmp(argv[0], "tabbed") == 0) {
		config->default_layout = L_TABBED;
	} else if (strcasecmp(argv[0], "vtabbed") == 0) {
		config->default_layout = L_VTABBED;
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected 'workspace_layout <default|stacking|tabbed|vtabbed>'");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
