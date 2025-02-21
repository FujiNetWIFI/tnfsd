#ifndef _MATCH_H
#define _MATCH_H

// set to 1 to enable dotglob: *. ?, and [] match a . (dotfile) at the begin or after each /
#define DOTGLOB 1

// set to 1 to enable case-insensitive glob matching
#define NOCASEGLOB 1

#define CASE(c) (NOCASEGLOB ? tolower(c) : (c))

int gitignore_glob_match(const char *text, const char *glob);

#endif