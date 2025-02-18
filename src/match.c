#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "fileinfo.h"
#include "match.h"

char _case(char c, bool ignore_case)
{
  return ignore_case ? tolower(c) : c;
}

// This function is copied from
// https://www.codeproject.com/Articles/5163931/Fast-String-Matching-with-Wildcards-Globs-and-Giti
//
// Author:      Robert A. van Engelen, engelen@genivia.com
// Date:        August 5, 2019
// License:     The Code Project Open License (CPOL)
//              https://www.codeproject.com/info/cpol10.aspx
//
// returns TRUE if text string matches gitignore-style glob pattern
int gitignore_glob_match(const char *text, const char *glob, bool ignore_case)
{
  const char *text1_backup = NULL;
  const char *glob1_backup = NULL;
  const char *text2_backup = NULL;
  const char *glob2_backup = NULL;
  int nodot = !DOTGLOB;
  // match pathname if glob contains a / otherwise match the basename
  if (*glob == '/')
  {
    // if pathname starts with ./ then ignore these pairs
    while (*text == '.' && text[1] == FILEINFO_PATHSEPARATOR)
      text += 2;
    // if pathname starts with a / then ignore it
    if (*text == FILEINFO_PATHSEPARATOR)
      text++;
    glob++;
  }
  else if (strchr(glob, '/') == NULL)
  {
    const char *sep = strrchr(text, FILEINFO_PATHSEPARATOR);
    if (sep)
      text = sep + 1;
  }
  while (*text != '\0')
  {
    switch (*glob)
    {
      case '*':
        // match anything except . after /
        if (nodot && *text == '.')
          break;
        if (*++glob == '*')
        {
          // trailing ** match everything after /
          if (*++glob == '\0')
            return true;
          // ** followed by a / match zero or more directories
          if (*glob != '/')
            return false;
          // new **-loop, discard *-loop
          text1_backup = NULL;
          glob1_backup = NULL;
          text2_backup = text;
          glob2_backup = ++glob;
          continue;
        }
        // trailing * matches everything except /
        text1_backup = text;
        glob1_backup = glob;
        continue;
      case '?':
        // match anything except . after /
        if (nodot && *text == '.')
          break;
        // match any character except /
        if (*text == FILEINFO_PATHSEPARATOR)
          break;
        text++;
        glob++;
        continue;
      case '[':
      {
        int lastchr;
        int matched;
        int reverse;
        // match anything except . after /
        if (nodot && *text == '.')
          break;
        // match any character in [...] except /
        if (*text == FILEINFO_PATHSEPARATOR)
          break;
        // inverted character class
        reverse = glob[1] == '^' || glob[1] == '!' ? true : false;
        if (reverse)
          glob++;
        // match character class
        matched = false;
        for (lastchr = 256; *++glob != '\0' && *glob != ']'; lastchr = _case(*glob, ignore_case))
          if (lastchr < 256 && *glob == '-' && glob[1] != ']' && glob[1] != '\0' ?
              _case(*text, ignore_case) <= _case(*++glob, ignore_case) && _case(*text, ignore_case) >= lastchr :
              _case(*text, ignore_case) == _case(*glob, ignore_case))
            matched = true;
        if (matched == reverse)
          break;
        text++;
        if (*glob != '\0')
          glob++;
        continue;
      }
      case '\\':
        // literal match \-escaped character
        glob++;
        // FALLTHROUGH
      default:
        // match the current non-NUL character
        if (_case(*glob, ignore_case) != _case(*text, ignore_case) && !(*glob == '/' && *text == FILEINFO_PATHSEPARATOR))
          break;
        // do not match a . with *, ? [] after /
        nodot = !DOTGLOB && *glob == '/';
        text++;
        glob++;
        continue;
    }
    if (glob1_backup != NULL && *text1_backup != FILEINFO_PATHSEPARATOR)
    {
      // *-loop: backtrack to the last * but do not jump over /
      text = ++text1_backup;
      glob = glob1_backup;
      continue;
    }
    if (glob2_backup != NULL)
    {
      // **-loop: backtrack to the last **
      text = ++text2_backup;
      glob = glob2_backup;
      continue;
    }
    return false;
  }
  // ignore trailing stars
  while (*glob == '*')
    glob++;
  // at end of text means success if nothing else is left to match
  return *glob == '\0' ? true : false;
}
