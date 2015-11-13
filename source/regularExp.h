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


#ifndef NEDIT_REGULAREXP_H_INCLUDED
#define NEDIT_REGULAREXP_H_INCLUDED

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <cstdarg>

class regex_error : public std::exception {
public:
	regex_error(const char *fmt, ...) {
		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		error_ = buf;
	}
	
public:
	virtual const char *what() const noexcept override {
		return error_.c_str();
	}
private:
	std::string error_;
};

/* Number of text capturing parentheses allowed. */

#define NSUBEXP 50

/* Structure to contain the compiled form of a regular expression plus
   pointers to matched text.  `program' is the actual compiled regex code. */

struct regexp {
public:
	regexp(const char *exp, int defaultFlags);
	regexp(const regexp &) = delete;
	regexp &operator=(const regexp &) = delete;
	~regexp();
		   
public:
	const char *startp_[NSUBEXP]; /* Captured text starting locations. */
	const char *endp_[NSUBEXP];   /* Captured text ending locations. */
	const char *extentpBW_;       /* Points to the maximum extent of text scanned by ExecRE in front of the string to achieve a match (needed because of positive look-behind.) */
	const char *extentpFW_;       /* Points to the maximum extent of text scanned by ExecRE to achieve a match (needed because of positive look-ahead.) */
	int top_branch_;              /* Zero-based index of the top branch that matches. Used by syntax highlighting only. */
	char match_start_;            /* Internal use only. */
	char anchor_;                 /* Internal use only. */
	uint8_t *program_;            /* Unwarranted chumminess with compiler. */
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
           const char *end,            /* Pointer to the end of `string'.  If NULL will
                                    scan from `string' until '\0' is found. */
           int reverse,                /* Backward search. */
           char prev_char,             /* Character immediately prior to `string'.  Set
                                          to '\n' or '\0' if true beginning of text. */
           char succ_char,             /* Character immediately after `end'.  Set
                                          to '\n' or '\0' if true beginning of text. */
           const char *delimiters,     /* Word delimiters to use (NULL for default) */
           const char *look_behind_to, /* Boundary for look-behind; defaults to
                                          "string" if NULL */
           const char *match_till);    /* Boundary to where match can extend.
                                          \0 is assumed to be the boundary if not
                                          set. Lookahead can cross the boundary. */

/* Perform substitutions after a `regexp' match. */
bool SubstituteRE(const regexp *prog, const char *source, char *dest, int max);

/* Builds a default delimiter table that persists across `ExecRE' calls that
   is identical to `delimiters'.  Pass NULL for "default default" set of
   delimiters. */

void SetREDefaultWordDelimiters(char *delimiters);

#endif /* NEDIT_REGULAREXP_H_INCLUDED */
