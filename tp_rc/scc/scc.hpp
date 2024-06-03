/*
@(#)File:           $RCSfile: scc.c,v $
@(#)Version:        $Revision: 8.3 $
@(#)Last changed:   $Date: 2022/05/30 01:02:22 $
@(#)Purpose:        Strip C comments
@(#)Author:         J Leffler
@(#)Copyright:      (C) JLSS 1991-2022
*/

/*TABSTOP=4*/

/*
**  The SCC processor removes any C comments and replaces them by a
**  single space.  It will be used as part of a formatting pipeline
**  for checking the equivalence of C code.
**
**  If the code won't compile, it is unwise to use this tool to modify
**  it.  It assumes that the code is syntactically correct.
**
**  Note that backslashes at the end of a line can extend even a C++
**  style comment over several lines.  It matters not whether there is
**  one backslash or several -- the line splicing (logically) takes
**  place before any other tokenisation.
**
**  The -s option was added to simplify the analysis of C++ keywords.
**  After stripping comments, keywords appearing in (double quoted)
**  strings should be ignored too, so a replacement character such as X
**  works.  Without that, the command has to recognize C comments to
**  know whether the unmatched quotes in them matter (they shouldn't).
**  The -q option was added for symmetry with -s.
**
**  Digraphs do not present a problem; the characters they represent do
**  not need special handling.  Trigraphs do present a problem in theory
**  because ??/ is an encoding for backslash.  However, this program
**  ignores trigraphs altogether - calling upon GCC for precedent, and
**  noting that C++14 has deprecated trigraphs.  The JLSS programs
**  digraphs and trigraphs can manipulate (encode, decode) digraphs and
**  trigraphs.  This more of a theoretical problem than a practical one.
**
**  C++14 adds quotes inside numeric literals: 10'000'000 for 10000000,
**  10'000 for 10000, etc.  Unfortunately, that means SCC has to
**  recognize literal values fully, because these can appear in hex too:
**  0xFFFF'ABCD.  C++14 also adds binary constants: 0b0001'1010 (b or
**  B).  (See N3797 for draft C++14 standard.)
**
**  C++11 raw strings are another problem: R"x(...)x" is not bound to
**  have a close quote by end of line.  (The parentheses are mandatory;
**  the x's are optional and are not limited to a single character (but
**  must not be more than 16 characters long), but must match.  The
**  replacable portion (for -s) is the '...' in between parentheses.)
**  C++11 also has encoding prefixes u8, u, U, L which can preceded the
**  R of a R string.  Note that within a raw string, there are no
**  trigraphs.
**
**  Hence we add -S <std> for standards C++98, C++03, C++11, C++14,
**  C++17, C89, C90, C99, C11, C18.  For the purposes of this code,
**  C++98 and C++03 are identical, C89 and C90 are identical, and C99,
**  C11 and C18 are identical.  The default is C18.
**
**  C11 and C++11 add support for u"literal", U"literal", u8"literal",
**  and for u'char' and U'char' (but not u8'char').  Previously, the
**  code needed no special handling for wide character strings L"x" or
**  constants L'x', but now they have to be handled appropriately.
**
**  Note that comment stripping does not require 100% accurate
**  tokenization.  For example, C++11 and later supports user-defined
**  literals such as 1.234_km; it does not matter that SCC treats that
**  as a number and an identifier, but a program that formally tokenizes
**  C++ must recognize them.
*/

//#include "posixver.h"
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSVC_LANG
#include <io.h>
#else
#include <unistd.h>
#endif
#include <cstdio>
#include <string>
#include <stdexcept>
#include <cstdarg>
//#include "filter.h"
//#include "scc-version.h"
//#include "stderr.h"

//##################################################################################################
class SCC
{
public:
  //################################################################################################
  enum class Standard
  {
    C, C89, C90, C94, C99, C11, C18,
    CXX, CXX98, CXX03, CXX11, CXX14, CXX17
  };

  //################################################################################################
  std::string standardToString(Standard standard)
  {
    switch(standard)
    {
      case Standard::C:     return "C";      // Current C standard (C18)
      case Standard::CXX:   return "C++";    // Current C++ standard (C++17)
      case Standard::C89:   return "C89";
      case Standard::C90:   return "C90";
      case Standard::C94:   return "C94";
      case Standard::C99:   return "C99";
      case Standard::C11:   return "C11";
      case Standard::C18:   return "C18";
      case Standard::CXX98: return "C++98";
      case Standard::CXX03: return "C++03";
      case Standard::CXX11: return "C++11";
      case Standard::CXX14: return "C++14";
      case Standard::CXX17: return "C++17";
    }
    return {};
  }

  //################################################################################################
  SCC(const std::string& input,
      Standard standard_,
      bool printCommentsNotCode_              = false,
      bool printEmptyCommentInsteadOfBlank_   = false,
      bool warnAboutNestedCStyleComments_     = false,
      char qchar_                             = 0,
      char schar_                             = 0):
    m_input(input),
    standard(standard_),
    printCommentsNotCode(printCommentsNotCode_),
    printEmptyCommentInsteadOfBlank(printEmptyCommentInsteadOfBlank_),
    warnAboutNestedCStyleComments(warnAboutNestedCStyleComments_),
    qchar(qchar_),
    schar(schar_)
  {
    try
    {
      itr = m_input.begin();
      set_features(standard);
      scc();
      m_ok = true;
    }
    catch(...)
    {
      m_ok = false;
      m_result.clear();
    }
  }

  //################################################################################################
  const std::string& result() const
  {
    return m_result;
  }

  //################################################################################################
  bool ok() const
  {
    return m_ok;
  }

private:
  typedef enum { NonComment, CComment, CppComment } Comment;

  enum Feature { F_HEXFLOAT, F_RAWSTRING, F_DOUBLESLASH, F_UNICODE, F_BINARY, F_NUMPUNCT, F_UNIVERSAL };
  std::string featureToNameString(Feature feature)
  {
    switch(feature)
    {
      case F_HEXFLOAT:    return "Hexadecimal floating point constant";
      case F_RAWSTRING:   return "Raw string";
      case F_DOUBLESLASH: return "Double slash comment";
      case F_UNICODE:     return "Unicode character or string";
      case F_BINARY:      return "Binary literal";
      case F_NUMPUNCT:    return "Numeric punctuation";
      case F_UNIVERSAL:   return "Universal character name";
    }

    return {};
  }

  enum { MAX_RAW_MARKER = 16 };
  enum { LPAREN = '(', RPAREN = ')' };

  static constexpr const char*  dq_reg_prefix[] = { "L", "u", "U", "u8", };
  static constexpr const char* dq_raw_prefix[] = { "R", "LR", "uR", "UR", "u8R" };
  enum { NUM_DQ_REG_PREFIX = sizeof(dq_reg_prefix) / sizeof(dq_reg_prefix[0]) };
  enum { NUM_DQ_RAW_PREFIX = sizeof(dq_raw_prefix) / sizeof(dq_raw_prefix[0]) };

  std::string m_input;
  std::string::const_iterator itr;

  Standard standard = Standard::C18;  /* Selected standard */

  bool printCommentsNotCode              = false;
  bool printEmptyCommentInsteadOfBlank   = false;
  bool warnAboutNestedCStyleComments     = false;

  char qchar = 0;   /* Replacement character for quotes */
  char schar = 0;   /* Replacement character for strings */

  /* Features recognized */
  bool f_DoubleSlash = false;  /* // comments */
  bool f_RawString = false;    /* Raw strings */
  bool f_Unicode = false;      /* Unicode strings (u\"A\", U\"A\", u8\"A\") */
  bool f_Binary = false;       /* Binary constants 0b0101 */
  bool f_HexFloat = false;     /* Hexadecimal floats 0x2.34P-12 */
  bool f_NumPunct = false;     /* Numeric punctuation 0x1234'5678 */
  bool f_Universal = false;    /* Universal character names \uXXXX and \Uxxxxxxxx */

  int nline = 0;   /* Line counter */
  int l_nest = 0;  /* Last line with a nested comment warning */
  int l_cend = 0;  /* Last line with a comment end warning */
  bool l_comment = false;  /* Line contained a comment - print newline in -c mode */

  std::string m_result;
  bool m_ok{false};

#ifndef lint
  /* Prevent over-aggressive optimizers from eliminating ID string */
  std::string jlss_id_scc_c = "@(#)$Id: scc.c,v 8.3 2022/05/30 01:02:22 jonathanleffler Exp $";
#endif /* lint */

  void throwError(const std::string& error)
  {
    throw std::runtime_error(error);
  }

  void putchar(char c)
  {
    m_result += c;
  }

  int getch()
  {
    if(itr == m_input.end())
      return EOF;

    int c = *itr;
    ++itr;

    if (c == '\n')
      nline++;
    return(c);
  }

  void ungetch(char c)
  {
    if (c == '\n')
      nline--;

    if(itr!=m_input.begin())
      --itr;
  }

  int peek()
  {
    int c;

    if ((c = getch()) != EOF)
      ungetch(c);
    return(c);
  }

  /* Put source code character */
  void s_putch(char c)
  {
    if(!printCommentsNotCode)
      putchar(c);

    if (c == '\n')
      l_comment = false;
  }

  /* Put comment (non-code) character */
  void c_putch(char c)
  {
    if(printCommentsNotCode)
      putchar(c);
  }

  /* Output string of statement characters */
  void s_putstr(const char *str)
  {
    char c;
    while ((c = *str++) != '\0')
      s_putch(c);
  }

  void warning(const char *str, int line)
  {
    printf("%d: %s\n", line, str);
  }

  void warning2(const char *s1, const char *s2, int line)
  {
    printf("%d: %s %s\n", line, s1, s2);
  }

  void warningv(const char *fmt, int line, ...)
  {
    char buffer[BUFSIZ];
    va_list args;
    va_start(args, line);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    warning(buffer, line);
  }

  void warn_feature(enum Feature feature)
  {
    assert(feature >= F_HEXFLOAT && feature <= F_UNIVERSAL);
    warningv("%s feature used but not supported in %s", nline,
             featureToNameString(feature).c_str(), standardToString(standard).c_str());
  }

  void put_quote_char(char q, char c)
  {
    if (q == '\'' && qchar != 0)
      s_putch(qchar);
    else if (q == '"' && schar != 0)
      s_putch(schar);
    else
      s_putch(c);
  }

  void put_quote_str(char q, const char *str)
  {
    char c;
    while ((c = *str++) != '\0')
      put_quote_char(q, c);
  }

  void endquote(char q, const char *msg)
  {
    int c1;

    while ((c1 = getch()) != EOF && c1 != q)
    {
      if (c1 == '\\')
      {
        int bs_count = 1;
        int c2;
        while ((c2 = getch()) != EOF && c2 == '\\')
          bs_count++;
        if (c2 == EOF)
        {
          /* Stream of backslashes and newline - bug in source code */
          for (int i = 0; i < bs_count; i++)
            put_quote_char(q, c1);
          break;
        }
        if (c2 == '\n')
        {
          /* The backslash-newline would be processed first */
          /* Echo the other backslashes and then emit backslash-newline */
          for (int i = 1; i < bs_count; i++)
            put_quote_char(q, c1);
          s_putch(c1);
          s_putch(c2);
        }
        else
        {
          /* Series of backslashes not ending BSNL */
          /* Emit pairs of backslashes - then work out what to do */
          for (int i = 0; i < bs_count - 1; i += 2)
            put_quote_str(q, "\\\\");

          if (bs_count % 2 == 0)
          {
            s_putch(c2);
            if (c2 == q)
              return;
          }
          else
          {
            put_quote_char(q, c1);
            put_quote_char(q, c2);
            if ((c2 == 'u' || c2 == 'U') && !f_Universal)
              warn_feature(F_UNIVERSAL);
          }
        }
      }
      else if (c1 == '\n')
      {
        put_quote_char(q, c1);
        warning2("newline in", msg, nline - 1);
        /* Heuristic recovery - assume close quote at end of line */
        return;
      }
      else
        put_quote_char(q, c1);
    }
    if (c1 == EOF)
    {
      warning2("EOF in", msg, nline);
      return;
    }
    s_putch(q);
  }

  /*
  ** read_bsnl() - Count the number of backslash newline pairs that
  ** immediately follow in the input stream.  On entry, peek() might
  ** return the backslash of a backslash newline pair, or some other
  ** character.  On exit, getch() will return the first character
  ** after the sequence of n (n >= 0) backslash newline pairs.  It
  ** relies on two characters of pushback (but only of data already
  ** read) - not mandated by POSIX or C standards.  Double pushback
  ** may cause problems on HP-UX, Solaris or AIX (but does not cause
  ** trouble on Linux or BSD).
  */
  int read_bsnl()
  {
    int n = 0;
    int c;

    while ((c = peek()) != EOF)
    {
      if (c != '\\')
        return(n);
      c = getch();              /* Read backslash */
      if ((c = peek()) != '\n') /* Single pushback */
      {
        ungetch('\\');      /* Double pushback */
        return(n);
      }
      n++;
      c = getch();              /* Read newline */
    }
    return(n);
  }

  void write_bsnl(int bsnl, void (SCC::*put)(char))
  {
    while (bsnl-- > 0)
    {
      (this->*put)('\\');
      (this->*put)('\n');
    }
  }

  Comment c_comment(int c)
  {
    Comment status = CComment;
    if (c == '*')
    {
      int bsnl = read_bsnl();
      if (peek() == '/')
      {
        l_comment = true;
        status = NonComment;
        c = getch();
        c_putch('*');
        write_bsnl(bsnl, &SCC::c_putch);
        c_putch('/');
        s_putch(' ');
        if(printEmptyCommentInsteadOfBlank)
        {
          s_putch('*');
          s_putch('/');
        }
      }
      else
      {
        c_putch(c);
        write_bsnl(bsnl, &SCC::c_putch);
      }
    }
    else if (warnAboutNestedCStyleComments && c == '/' && peek() == '*')
    {
      if (l_nest != nline)
        warning("nested C-style comment", nline);
      l_nest = nline;
      c_putch(c);
    }
    else
      c_putch(c);
    return status;
  }

  Comment cpp_comment(int c, int oc)
  {
    Comment status = CppComment;
    if (c == '\n' && oc != '\\')
    {
      status = NonComment;
      s_putch(c);
    }
    else
      c_putch(c);
    return status;
  }

  /* Backslash was read but not printed! u or U was peeked but not read */
  /*
  ** There's no compelling reason to handle UCNs - the code is supposed to
  ** be valid before using SCC on it, and invalid UCNs therefore should
  ** not appear.  OTOH, to report their use when not supported, you have
  ** to detect their existence.
  */
  void scan_ucn(int letter, int nbytes)
  {
    assert(letter == 'u' || letter == 'U');
    assert(nbytes == 4 || nbytes == 8);
    bool ok = true;
    int i;
    char str[8];
    if (!f_Universal)
      warn_feature(F_UNIVERSAL);
    s_putch('\\');
    int c = getch();
    assert(c == letter);
    s_putch(c);
    for (i = 0; i < nbytes; i++)
    {
      c = getch();
      if (c == EOF)
      {
        ok = false;
        break;
      }
      if (!isxdigit(c))
      {
        ok = false;
        s_putch(c);
        break;
      }
      str[i] = c;
      s_putch(c);
    }
    if (!ok)
    {
      char msg[64];
      snprintf(msg, sizeof(msg), "Invalid UCN \\%c%.*s%c detected", letter, i, str, c);
      warning(msg, nline);
    }
  }

  static inline int is_idchar(int c)
  {
    assert(c == EOF || (c >= 0 && c <= UCHAR_MAX));
    return(isalnum(c) || c == '_');
  }

  static inline int is_binary(int c)
  {
    assert(c == EOF || (c >= 0 && c <= UCHAR_MAX));
    return(c == '0' || c == '1');
  }

  static inline int is_octal(int c)
  {
    assert(c == EOF || (c >= 0 && c <= UCHAR_MAX));
    return(c >= '0' && c <= '7');
  }

  int check_punct(int oc, int (*digit_check)(int c))
  {
    int sq = getch();
    assert(sq == '\'');
    s_putch(sq);
    if (!f_NumPunct)
      warn_feature(F_NUMPUNCT);
    if (!(*digit_check)(oc))
    {
      warning("Single quote in numeric context not preceded by a valid digit", nline);
      return sq;
    }
    int pc = peek();
    if (pc == EOF)
    {
      warning("Single quote in numeric context followed by EOF", nline);
      return sq;
    }
    if (!(*digit_check)(pc))
      warning("Single quote in numeric context not followed by a valid digit", nline);
    return pc;
  }

  inline void parse_exponent()
  {
    /* First character is known to be valid exponent (p, P, e, E) */
    int c = getch();
    assert(c == 'e' || c == 'E' || c == 'p' || c == 'P');
    s_putch(c);
    int pc = peek();
    int count = 0;
    if (pc == '+' || pc == '-')
      s_putch(getch());
    while ((pc = peek()) != EOF && isdigit(pc))
    {
      count++;
      s_putch(getch());
    }
    if (count == 0)
    {
      char msg[80];
      snprintf(msg, sizeof(msg), "Exponent %c not followed by (optional sign and) one or more digits", c);
      warning(msg, nline);
    }
  }

  void parse_hex()
  {
    /* Hex constant - integer or float */
    /* Should be followed by one or more hex digits */
    s_putch('0');
    int c = getch();
    assert(c == 'x' || c == 'X');
    s_putch(c);
    int oc = c;
    int pc;
    bool warned = false;
    while ((pc = peek()) == '\'' || isxdigit(pc) || pc == '.')
    {
      if (pc == '\'')
        oc = check_punct(oc, isxdigit);
      else
      {
        if (pc == '.' && !f_HexFloat)
        {
          if (!warned)
            warn_feature(F_HEXFLOAT);
          warned = true;
        }
        oc = pc;
        s_putch(getch());
      }
    }
    if (pc == 'p' || pc == 'P')
    {
      if (!f_HexFloat && !warned)
        warn_feature(F_HEXFLOAT);
      parse_exponent();
    }
  }

  void parse_binary()
  {
    /* Binary constant - integer */
    /* Should be followed by one or more binary digits */
    if (!f_Binary)
      warn_feature(F_BINARY);
    s_putch('0');     /* 0 */
    int c = getch();
    assert(c == 'b' || c == 'B');
    s_putch(c);     /* b or B */
    int oc = c;
    int pc;
    while ((pc = peek()) == '\'' || is_binary(pc))
    {
      if (pc == '\'')
        oc = check_punct(oc, is_binary);
      else
      {
        oc = pc;
        s_putch(getch());
      }
    }
    if (isdigit(pc))
      warningv("Non-binary digit %c in binary constant", nline, pc);
  }

  void parse_octal()
  {
    /* Octal constant - integer */
    /* Calling code checked for octal digit or s-quote */
    s_putch('0');     /* 0 */
    int c = getch();
    assert(is_octal(c) || c == '\'');
    s_putch(c);
    int oc = c;
    int pc;
    while ((pc = peek()) == '\'' || is_octal(pc))
    {
      if (pc == '\'')
        oc = check_punct(oc, is_octal);
      else
      {
        oc = pc;
        s_putch(getch());
      }
    }
    if (isdigit(pc))
      warningv("Non-octal digit %c in octal constant", nline, pc);
  }

  void parse_decimal(int c)
  {
    /* Decimal integer, or decimal floating point */
    s_putch(c);
    int pc = peek();
    if (isdigit(pc) || pc == '\'')
    {
      c = getch();
      assert(c == pc);
      s_putch(pc);
      int oc = c;
      while ((pc = peek()) == '\'' || isdigit(pc))
      {
        /* Assuming isdigit alone generates a function pointer */
        if (pc == '\'')
          oc = check_punct(oc, isdigit);
        else
        {
          oc = pc;
          s_putch(getch());
        }
      }
      if (pc == 'e' || pc == 'E')
        parse_exponent();
    }
  }

  /*
  ** Parse numbers - inherently unsigned.
  ** Need to recognize:-
  ** 12345            // Decimal
  ** 01234567         // Octal - validation not required?
  ** 0xABCDEF12       // Hexadecimal
  ** 0b01101100       // C++14 binary
  ** 9e-82            // Float
  ** 9.23             // Float
  ** .987             // Float
  ** .987E+30         // Float
  ** 0xA.BCP12        // C99 Hex floating point
  ** 0B0110'1100      // C++14 punctuated binary number
  ** 0XDEFA'CED0      // C++14 punctuated hex number
  ** 234'567.123'987  // C++14 punctuated decimal number
  ** 0'234'127'310    // C++14 punctuated octal number
  ** 9'234.192'214e-8 // C++14 punctuated decimal Float
  ** 0xA'B'C.B'Cp-12  // C++17 Presumed punctuated Hex floating point
  **
  ** Note that backslash-newline can occur part way through a number.
  */

  void parse_number(int c)
  {
    assert(isdigit(c) || c == '.');
    int pc = peek();
    if (c != '0')
      parse_decimal(c);
    else if (pc == 'x' || pc == 'X')
      parse_hex();
    else if ((pc == 'b' || pc == 'B'))
      parse_binary();
    else if (is_octal(pc) || pc == '\'')
      parse_octal();
    else if (pc == 'e' || pc == 'E' || pc == '.')
    {
      /* Simple fractional (0.1234) or zero floating point decimal constant 0E0 */
      parse_decimal(c);
    }
    else if (isdigit(pc))
    {
      /*
        ** Malformed number of some sort (09, for example).
        ** Preprocessing numbers can contain all sorts of weird stuff.
        ** 08 is valid as a preprocessing number, and has appeared in
        ** macros.  It ended up as part of "%08X" or similar format
        ** strings.  Hence, do not generate error message after all.
        ** err_remark("0%c read - bogus number!\n", pc);
        */
      s_putch(c);
    }
    else
    {
      /* Just a zero? -- e.g. array[0] */
      s_putch(c);
    }
  }

  void read_remainder_of_identifier()
  {
    int c;
    while ((c = peek()) != EOF && is_idchar(c))
    {
      c = getch();
      s_putch(c);
    }
  }

  inline bool could_be_string_literal(char c)
  {
    return(c == 'U' || c == 'u' || c == 'L' || c == 'R' || c == '8');
  }

  bool valid_dq_raw_prefix(const char *prefix)
  {
    for (int i = 0; i < NUM_DQ_RAW_PREFIX; i++)
    {
      if (strcmp(prefix, dq_raw_prefix[i]) == 0)
        return true;
    }
    return false;
  }

  bool valid_dq_reg_prefix(const char *prefix)
  {
    for (int i = 0; i < NUM_DQ_REG_PREFIX; i++)
    {
      if (strcmp(prefix, dq_reg_prefix[i]) == 0)
        return true;
    }
    return false;
  }

  bool valid_dq_prefix(const char *prefix)
  {
    return valid_dq_reg_prefix(prefix) || valid_dq_raw_prefix(prefix);
  }

  bool raw_scan_marker(char *markstr, int *marklen, const char *pfx)
  {
    int len = 0;
    int c;
    char message[128];
    while ((c = getch()) != EOF)
    {
      if (c == LPAREN)
      {
        /* End of marker */
        assert(len <= MAX_RAW_MARKER);
        markstr[len] = '\0';
        *marklen = len;
        return true;
      }
      else if (strchr("\") \\\t\v\f\n", c) != 0 || len >= MAX_RAW_MARKER)
      {
        /* Invalid mark character or marker is too long */
        if (len >= MAX_RAW_MARKER)
        {
          markstr[len++] = c;
          markstr[len] = '\0';
          snprintf(message, sizeof(message),
                   "Too long a raw string d-char-sequence: %s\"%.*s",
                   pfx, len, markstr);
        }
        else
        {
          char qc[10] = "";
          if (isgraph(c))
            snprintf(qc, sizeof(qc), " '%s%c'",
                     ((c == '\'' || c == '\\') ? "\\" : ""), c);
          snprintf(message, sizeof(message),
                   "Invalid mark character (code %d%s) in d-char-sequence: %s\"%.*s",
                   c, qc, pfx, len, markstr);
        }
        warning(message, nline);
        markstr[len++] = c;
        markstr[len] = '\0';
        *marklen = len;
        return false;
      }
      else
        markstr[len++] = c;
    }
    snprintf(message, sizeof(message),
             "Unexpected EOF in raw string d-char-sequence: %s\"%.*s",
             pfx, len, markstr);
    warning(message, nline);
    markstr[len] = '\0';
    *marklen = len;
    return false;
  }

  /* Look for ) followed by markstr and double quote */
  void raw_scan_string(const char *markstr, int marklen, int line1)
  {
    int c;

    while ((c = getch()) != EOF)
    {
      if (c != RPAREN)
        s_putch(c);
      else
      {
        char endstr[MAX_RAW_MARKER + 2];
        int len = 0;
        while ((c = getch()) != EOF)
        {
          if (c == '"' && len == marklen)
          {
            /* Got the end! */
            s_putch(RPAREN);
            s_putstr(markstr);
            s_putch(c);
            return;
          }
          else if (c == markstr[len])
            endstr[len++] = c;
          else if (c == RPAREN)
          {
            /* Restart scan for mark string */
            endstr[len] = '\0';
            s_putch(RPAREN);
            s_putstr(endstr);
            len = 0;
          }
          else
          {
            endstr[len] = '\0';
            s_putch(RPAREN);
            s_putstr(endstr);
            s_putch(c);
            break;
          }
        }
      }
    }
    warning("Unexpected EOF in raw string starting at this line", line1);
  }

  void parse_raw_string(const char *prefix)
  {
    /*
    ** Have read up to and including the double quote at the start of a
    ** raw string literal (u8R" for example) and prefix but not double
    ** quote has been printed.  Now find lead mark and open parenthesis.
    ** NB: lead mark is not allowed to be longer than 16 characters.
    ** u8R"lead(data)lead" is valid, as is u8R"(data)".
    **
    ** In the standard, the lead mark characters are called 'd-char's: any
    ** member of the basic source character set except: space, the left
    ** parenthesis (, the right parenthesis ), the backslash \, and the
    ** control characters representing horizontal tab, vertical tab,
    ** form feed, and newline.
    **
    ** A string literal that has an R in the prefix is a raw string
    ** literal.  The d-char-sequence serves as a delimiter.  The
    ** terminating d-char-sequence of a raw-string is the same sequence
    ** of characters as the initial d-charsequence.  A d-char-sequence
    ** shall consist of at most 16 characters.
    **
    ** NB: The fact that backslash is not allowed in the marker means
    **     that double quote is also prohibited since it would have to
    **     be preceded by a backslash.
    **
    ** Processing:
    ** 1. Find valid lead mark - up to first (.
    ** 2. If invalid, report as such and process as ordinary dq-string.
    ** 3. Else find ) and lead mark followed by close dq.
    **    - NB: R"aa( )aa )aa" is valid; the first ")aa" is content
    **      because it is not followed by a double quote.
    ** 4. If EOF encountered first, report the problem.
    ** Save nline for start of literal.
    **
    ** NB: If replacing string characters, the raw string delimiters are
    **     printed unmapped, but the body of the raw string is printed
    **     as the replacement character.
    */
    assert(prefix != 0);
    char markstr[MAX_RAW_MARKER + 2];
    int  marklen;
    if (raw_scan_marker(markstr, &marklen, prefix))
    {
      s_putch('"');
      s_putstr(markstr);
      s_putch(LPAREN);
      raw_scan_string(markstr, marklen, nline);
    }
    else
    {
      s_putch('"');
      put_quote_str('"', markstr);
      endquote('"', "string literal");
    }
  }

  void parse_dq_string(const char *prefix)
  {
    assert(valid_dq_prefix(prefix));
    if (valid_dq_raw_prefix(prefix))
    {
      if (!f_RawString)
        warn_feature(F_RAWSTRING);
      s_putstr(prefix);
      parse_raw_string(prefix);
    }
    else
    {
      if (strcmp(prefix, "L") != 0 && !f_Unicode)
        warn_feature(F_UNICODE);
      s_putstr(prefix);
      s_putch('"');
      endquote('"', "string literal");
    }
  }

  void process_poss_string_literal(char c)
  {
    char prefix[6] = "";
    int idx = 0;
    prefix[idx++] = c;
    while ((c = peek()) != EOF)
    {
      if (c == '\'')
      {
        /* process sinqle quote */
        /* Curiously, it really doesn't matter if the prefix is valid or not */
        /* SCC will process it the same way, printing prefix and then processing single quote */
        s_putstr(prefix);
        c = getch();
        s_putch(c);
        endquote(c, "character constant");
        break;
      }
      else if (c == '"')
      {
        /* process double quote - possibly raw */
        if (valid_dq_prefix(prefix))
        {
          c = getch();
          parse_dq_string(prefix);
        }
        else
        {
          /* Invalid syntax - identifier followed by double quote */
          s_putstr(prefix);
          c = getch();
          s_putch(c);
          endquote(c, "character constant");
        }
        break;
      }
      else if (could_be_string_literal(c))
      {
        c = getch();
        prefix[idx++] = c;
        if (idx > 3)
        {
          s_putstr(prefix);
          read_remainder_of_identifier();
          break;
        }
        /* Only loop continuation */
      }
      else
      {
        s_putstr(prefix);
        read_remainder_of_identifier();
        break;
      }
    }
  }

  /*
  ** Parse identifiers.
  ** Also parse strings and characters preceded by alphanumerics (raw
  ** strings, Unicode strings, and some character literals).
  ** L"xxx" in all standard variants of C and C++.
  ** u"xxx", U"xxx", u8"xxx from C11 and C++11 onwards.
  ** R"y(xxx)y", LR"y(xxx)y", uR"y(xxx)y", UR"y(xxx)y" and u8R"y(xxx)y" in C++11 onwards.
  ** L'x' in all standard variants of C and C++.
  ** U'x' and u'x' from C11 and C++11 onwards.
  ** No space is allowed between the prefix and the quote.
  **
  ** NB: UCNs in an identifier are parsed independently of 'identifier'.
  */
  void parse_identifier(int c)
  {
    assert(isalpha(c) || c == '_');
    if (could_be_string_literal(c))
      process_poss_string_literal(c);
    else
    {
      s_putch(c);
      read_remainder_of_identifier();
    }
  }

  Comment non_comment(int c)
  {
    int pc;
    Comment status = NonComment;
    if (c == '*')
    {
      int bsnl = read_bsnl();
      if ((pc = peek()) == '/')
      {
        c = getch();
        s_putch('*');
        write_bsnl(bsnl, &SCC::s_putch);
        s_putch('/');
        if (l_cend != nline)
          warning("C-style comment end marker ('*/') not in a comment", nline);
        l_cend = nline;
      }
      else
      {
        s_putch(c);
        write_bsnl(bsnl, &SCC::s_putch);
      }
    }
    else if (c == '\'')
    {
      s_putch(c);
      /*
        ** Single quotes can contain multiple characters, such as
        ** '\\', '\'', '\377', '\x4FF', 'ab', '/ *' (with no space, and
        ** the reverse character pair) , etc.  Scan for an unescaped
        ** closing single quote.  Newlines are not acceptable either,
        ** unless preceded by a backslash -- so both '\<nl>\n' and
        ** '\\<nl>n' are OK, and are equivalent to a newline character
        ** (when <nl> is a physical newline in the source code).
        */
      endquote(c, "character constant");
    }
    else if (c == '"')
    {
      s_putch(c);
      /* Double quotes are relatively simple, except that */
      /* they can legitimately extend over several lines */
      /* when each line is terminated by a backslash */
      endquote(c, "string literal");
    }
    else if (c == '/')
    {
      /* Potential start of comment */
      int bsnl = read_bsnl();
      if ((pc = peek()) == '*')
      {
        status = CComment;
        c = getch();
        c_putch('/');
        write_bsnl(bsnl, &SCC::c_putch);
        c_putch('*');
        if(printEmptyCommentInsteadOfBlank)
        {
          s_putch('/');
          s_putch('*');
        }
      }
      else if (!f_DoubleSlash && pc == '/')
      {
        warn_feature(F_DOUBLESLASH);
        c = getch();
        s_putch(c);
        write_bsnl(bsnl, &SCC::s_putch);
        s_putch(c);
      }
      else if (f_DoubleSlash && pc == '/')
      {
        status = CppComment;
        c = getch();
        c_putch(c);
        write_bsnl(bsnl, &SCC::c_putch);
        c_putch(c);
        if(printEmptyCommentInsteadOfBlank)
          s_putstr("//");
      }
      else
      {
        s_putch(c);
        write_bsnl(bsnl, &SCC::s_putch);
      }
    }
    else if (isdigit(c) || (c == '.' && isdigit(peek())))
      parse_number(c);
    else if (isalnum(c) || c == '_')
      parse_identifier(c);
    else if (c == '\\' && ((pc = peek()) == 'u' || pc == 'U'))
      scan_ucn(pc, (pc == 'u' ? 4 : 8));
    else
    {
      /* space, punctuation, ... */
      s_putch(c);
    }
    return status;
  }

  void scc()
  {
    int oc;
    int c;
    Comment status = NonComment;

    l_nest = 0; /* Last line with a nested comment warning */
    l_cend = 0; /* Last line with a comment end warning */
    nline = 1;

    for (oc = '\0'; (c = getch()) != EOF; oc = c)
    {
      switch (status)
      {
        case CComment:
        status = c_comment(c);
        break;
        case CppComment:
        status = cpp_comment(c, oc);
        break;
        case NonComment:
        status = non_comment(c);
        break;
      }
    }
    if (status != NonComment)
      warning("unterminated C-style comment", nline);
  }

  void set_features(Standard code)
  {
    switch (code)
    {
      case Standard::C89:
      case Standard::C90:
      case Standard::C94:
      break;
      case Standard::C:                     /* Current C standard is C18 */
      case Standard::C11:
      case Standard::C18:
      f_Unicode = true;
      /*FALLTHROUGH*/
      case Standard::C99:
      f_HexFloat = true;
      f_Universal = true;
      f_DoubleSlash = true;
      break;
      case Standard::CXX:                   /* Current C++ standard is C++17 */
      case Standard::CXX17:
      f_HexFloat = true;
      /*FALLTHROUGH*/
      case Standard::CXX14:
      f_Binary = true;
      f_NumPunct = true;
      /*FALLTHROUGH*/
      case Standard::CXX11:
      f_RawString = true;
      f_Unicode = true;
      /*FALLTHROUGH*/
      case Standard::CXX98:
      case Standard::CXX03:
      f_Universal = true;
      f_DoubleSlash = true;
      break;
      default:
      throwError("Invalid standard code");
      break;
    }
  }

  void print_features(Standard code)
  {
    printf("Standard: %s\n", standardToString(code).c_str());
    if (f_DoubleSlash)
      printf("Feature:  Double slash comments // to EOL\n");
    if (f_RawString)
      printf("Feature:  Raw strings R\"ZZ(string)ZZ\"\n");
    if (f_Unicode)
      printf("Feature:  Unicode strings (u\"A\", U\"A\", u8\"A\")\n");
    if (f_Binary)
      printf("Feature:  Binary constants 0b0101\n");
    if (f_HexFloat)
      printf("Feature:  Hexadecimal floats 0x2.34P-12\n");
    if (f_NumPunct)
      printf("Feature:  Numeric punctuation 0x1234'5678\n");
    if (f_Universal)
      printf("Feature:  Universal character names \\uXXXX and \\Uxxxxxxxx\n");
  }
};
