/*******************************************************************************
*                                                                              *
* regularExp.h -- Nirvana Editor Regular Expression Package Header File        *
*                                                                              *
* Copyright 2002 The NEdit Developers                                          *
*                                                                              *
* This is free software; you can redistribute it and/or modify it under the    *
* terms of the GNU General Public License as published by the Free Software    *
* Foundation; either version 2 of the License, or (at your option) any later   *
* version. In addition, you may distribute versions of this program linked to  *
* Motif or Open Motif. See README for details.                                 *
*                                                                              *
* This software is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for    *
* more details.                                                                *
*                                                                              *
* You should have received a copy of the GNU General Public License along with *
* software; if not, write to the Free Software Foundation, Inc., 59 Temple     *
* Place, Suite 330, Boston, MA  02111-1307 USA                                 *
*                                                                              *
* Nirvana Text Editor                                                          *
* July 31, 2001                                                                *
*                                                                              *
*******************************************************************************/

#include <X11/Intrinsic.h>
#include <cstdint>
#include "regex_error.h"
#include "string_view.h"

#ifndef REGULAREXP_H_
#define REGULAREXP_H_

/* Number of text capturing parentheses allowed. */

#define NSUBEXP 50u

/* Structure to contain the compiled form of a regular expression plus
   pointers to matched text.  `program' is the actual compiled regex code. */

class regexp {
public:
	regexp(const char *exp, int defaultFlags);
	~regexp();

public:
	const char *startp[NSUBEXP]; /* Captured text starting locations. */
	const char *endp[NSUBEXP];   /* Captured text ending locations. */
	const char *extentpBW;       /* Points to the maximum extent of text scanned by ExecRE in front of the string to achieve a match (needed because of positive look-behind.) */
	const char *extentpFW;       /* Points to the maximum extent of text scanned by ExecRE to achieve a match (needed because of positive look-ahead.) */
	int top_branch;              /* Zero-based index of the top branch that matches. Used by syntax highlighting only. */
	char match_start;            /* Internal use only. */
	char anchor;                 /* Internal use only. */
	uint8_t *program;
};

/* Flags for CompileRE default settings (Markus Schwarzenberg) */

enum RE_DEFAULT_FLAG {
	REDFLT_STANDARD = 0,
	REDFLT_CASE_INSENSITIVE = 1
	/* REDFLT_MATCH_NEWLINE = 2    Currently not used. */
};

/* Match a `regexp' structure against a string. */

int ExecRE(regexp *prog,               /* Compiled regex. */
           const char *string,         /* Text to search within. */
           const char *end,            /* Pointer to the end of `string'.  If NULL will scan from `string' until '\0' is found. */
           bool reverse,               /* Backward search. */
           char prev_char,             /* Character immediately prior to `string'.  Set to '\n' or '\0' if true beginning of text. */
           char succ_char,             /* Character immediately after `end'.  Set to '\n' or '\0' if true beginning of text. */
           const char *delimiters,     /* Word delimiters to use (NULL for default) */
           const char *look_behind_to, /* Boundary for look-behind; defaults to "string" if NULL */
           const char *match_till);    /* Boundary to where match can extend. \0 is assumed to be the boundary if not set. Lookahead can cross the boundary. */

/* Perform substitutions after a `regexp' match. */
bool SubstituteRE(const regexp *prog, const char *source, char *dest, const int max);

/* Builds a default delimiter table that persists across `ExecRE' calls that
   is identical to `delimiters'.  Pass NULL for "default default" set of
   delimiters. */

void SetREDefaultWordDelimiters(view::string_view delimiters);

#endif
