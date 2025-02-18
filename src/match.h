#ifndef _MATCH_H
#define _MATCH_H

// set to 1 to enable dotglob: *. ?, and [] match a . (dotfile) at the begin or after each /
#define DOTGLOB 1

char _case(char c, bool ignore_case);

int gitignore_glob_match(const char *text, const char *glob, bool ignore_case);

#endif