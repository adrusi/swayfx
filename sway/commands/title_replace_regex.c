#define _POSIX_C_SOURCE 200809L
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sway/commands.h"
#include "sway/tree/view.h"

struct cmd_results *cmd_title_replace_regex(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "title_replace_regex", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct sway_container *con = config->handler_context.container;
	if (!con) {
		return cmd_results_new(CMD_INVALID,
				"Only valid containers can have a title_replace_regex");
	}

	const char *src = argv[0];

	// Expect s<delim>pattern<delim>replacement<delim>[g]
	if (src[0] != 's' || src[1] == '\0') {
		return cmd_results_new(CMD_INVALID,
				"Expected 's/pattern/replacement/[g]'");
	}
	char delim = src[1];

	const char *p1 = src + 2;            // start of pattern
	const char *p2 = strchr(p1, delim);  // end of pattern
	if (!p2) {
		return cmd_results_new(CMD_INVALID,
				"Expected 's/pattern/replacement/[g]'");
	}
	const char *p3 = p2 + 1;            // start of replacement
	const char *p4 = strchr(p3, delim); // end of replacement
	if (!p4) {
		return cmd_results_new(CMD_INVALID,
				"Expected 's/pattern/replacement/[g]'");
	}
	const char *flags = p4 + 1;

	char *pattern     = strndup(p1, (size_t)(p2 - p1));
	char *replacement = strndup(p3, (size_t)(p4 - p3));
	if (!pattern || !replacement) {
		free(pattern);
		free(replacement);
		return cmd_results_new(CMD_FAILURE, "Allocation failure");
	}

	int errcode;
	PCRE2_SIZE erroffset;
	pcre2_code *regex = pcre2_compile(
			(PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
			PCRE2_UTF | PCRE2_UCP, &errcode, &erroffset, NULL);
	free(pattern);

	if (!regex) {
		PCRE2_UCHAR8 errbuf[256];
		pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
		free(replacement);
		return cmd_results_new(CMD_FAILURE,
				"title_replace_regex: regex compile error: %s", (char *)errbuf);
	}

	if (con->title_replace_regex) {
		pcre2_code_free((pcre2_code *)con->title_replace_regex);
	}
	free(con->title_replace_repl);

	con->title_replace_regex  = (void *)regex;
	con->title_replace_repl   = replacement;
	con->title_replace_global = (strchr(flags, 'g') != NULL);

	if (con->view) {
		view_update_title(con->view, true);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
