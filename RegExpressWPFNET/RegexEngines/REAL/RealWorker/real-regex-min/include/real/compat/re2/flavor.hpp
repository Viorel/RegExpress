/*!
 * \file re2/flavor.hpp
 * \brief `real::compat::re2` — RE2's reading of the syntax REAL reads as Python's, rewritten into REAL's.
 *
 * RE2's `\w \d \s` are ASCII, its `\b \B` read ASCII word characters, and it reads the POSIX classes
 * (`[[:alpha:]]`) inside a bracket. Under case folding RE2 folds a class member before negating it, the
 * member's own negation included: `(?i)[\W]` leaves out `k` and the Kelvin sign alike. The rewrite gives
 * REAL the same sets as written members, which REAL folds as RE2 does.
 */
#ifndef REAL_RE2_FLAVOR_HPP
#define REAL_RE2_FLAVOR_HPP

// Internal — do not include directly; the entry point is <real/compat/re2/re2.hpp>.

#include <real/version.hpp>

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <real/unicode/unicode_fold.hpp>
#include <real/unicode/utf8.hpp>

namespace real::compat::re2::detail {

  //! An ASCII set, one bit per byte below 0x80.
  using ascii_set = std::bitset<0x80>;

  /*!
   * \brief The ASCII set a shorthand letter or a POSIX class name stands for, as RE2 defines it.
   * \param[in]  name A shorthand letter (`w`, `d`, `s`) or a POSIX class name (`alpha`, `word`, ...).
   * \param[out] set  Receives the set.
   * \return `false` for a name RE2 does not define.
   */
  inline bool re2_ascii_set(std::string_view name,
                            ascii_set&       set)
  {
    set.reset();
    const auto range {[&set](unsigned lo, unsigned hi) {
                        for (unsigned c {lo}; c <= hi; ++c) {
                          set.set(c);
                        }
                      }};
    if (name == "d" || name == "digit") {
      range('0', '9');
    }
    else if (name == "w" || name == "word") {
      range('0', '9');
      range('A', 'Z');
      range('a', 'z');
      set.set('_');
    }
    else if (name == "s") {
      for (const char c : {'\t', '\n', '\f', '\r', ' '}) {
        set.set(static_cast<unsigned char>(c));
      }
    }
    else if (name == "space") {
      range('\t', '\r');
      set.set(' ');
    }
    else if (name == "alnum") {
      range('0', '9');
      range('A', 'Z');
      range('a', 'z');
    }
    else if (name == "alpha") {
      range('A', 'Z');
      range('a', 'z');
    }
    else if (name == "ascii") {
      range(0x00, 0x7F);
    }
    else if (name == "blank") {
      set.set('\t');
      set.set(' ');
    }
    else if (name == "cntrl") {
      range(0x00, 0x1F);
      set.set(0x7F);
    }
    else if (name == "graph") {
      range(0x21, 0x7E);
    }
    else if (name == "print") {
      range(0x20, 0x7E);
    }
    else if (name == "lower") {
      range('a', 'z');
    }
    else if (name == "upper") {
      range('A', 'Z');
    }
    else if (name == "punct") {
      range('!', '/');
      range(':', '@');
      range('[', '`');
      range('{', '~');
    }
    else if (name == "xdigit") {
      range('0', '9');
      range('A', 'F');
      range('a', 'f');
    }
    else {
      return false;
    }
    return true;
  }

  /*!
   * \brief Appends \p cp as a class member REAL reads in any position: `\x{...}`.
   * \param[in,out] out The class text.
   * \param[in]     cp  The code point.
   */
  inline void append_code_point(std::string&  out,
                                std::uint32_t cp)
  {
    char buffer[16] {};
    static_cast<void>(std::snprintf(buffer, sizeof buffer, "\\x{%X}", static_cast<unsigned>(cp)));
    out += buffer;
  }

  /*!
   * \brief Appends the members of \p set as class ranges.
   * \param[in,out] out The class text.
   * \param[in]     set The set.
   */
  inline void append_set(std::string&     out,
                         const ascii_set& set)
  {
    for (std::uint32_t c {0}; c < 0x80U; ++c) {
      if (!set.test(c)) {
        continue;
      }
      std::uint32_t last {c};
      while (last + 1U < 0x80U && set.test(last + 1U)) {
        ++last;
      }
      append_code_point(out, c);
      if (last != c) {
        out += '-';
        append_code_point(out, last);
      }
      c = last;
    }
  }

  /*!
   * \brief Appends, as class ranges, every scalar value outside \p set and, under \p icase, outside the case
   *        partners of its members: the class member RE2 reads for `\W` or `[:^alpha:]`.
   * \param[in,out] out   The class text.
   * \param[in]     set   The set being negated.
   * \param[in]     icase Whether the member is folded before it is negated.
   */
  inline void append_complement(std::string&     out,
                                const ascii_set& set,
                                bool             icase)
  {
    std::vector<std::uint32_t> excluded;
    for (std::uint32_t c {0}; c < 0x80U; ++c) {
      if (!set.test(c)) {
        continue;
      }
      excluded.push_back(c);
      const std::size_t index {real::detail::find_fold_index(c)};
      if (icase && index != real::detail::unicode_fold_table_size) {
        const real::detail::fold_entry& entry {real::detail::unicode_fold_table[index]};
        for (std::uint8_t k {0}; k < entry.count; ++k) {
          excluded.push_back(entry.partner[k]);
        }
      }
    }
    excluded.push_back(0xD800U); // surrogates, a block of their own: no scalar value to name there
    std::ranges::sort(excluded);
    std::uint32_t next {0};
    const auto    emit {[&out](std::uint32_t lo, std::uint32_t hi) {
                          append_code_point(out, lo);
                          if (hi != lo) {
                            out += '-';
                            append_code_point(out, hi);
                          }
                        }};
    for (const std::uint32_t cp : excluded) {
      const std::uint32_t skip_to {cp == 0xD800U ? 0xE000U : cp + 1U};
      if (cp > next) {
        emit(next, cp - 1U);
      }
      next = std::max(next, skip_to);
    }
    emit(next, 0x10FFFFU);
  }

  /*!
   * \brief The length of the character at \p pos: its UTF-8 sequence when whole, else one byte (RE2 steps
   *        over an invalid byte alone). One past the end of \p text, one.
   * \param[in] text The text.
   * \param[in] pos  A position in \p text, or its end.
   * \return How far the next character starts from \p pos.
   */
  [[nodiscard]] inline std::size_t next_character_length(std::string_view text,
                                                         std::size_t      pos)
  {
    if (pos >= text.size()) {
      return 1;
    }
    const real::detail::decoded_codepoint decoded {real::detail::decode_codepoint_strict(text, pos)};
    return decoded.valid ? decoded.length : 1U;
  }

  /*!
   * \brief Rewrites \p pattern from RE2's flavor into REAL's: ASCII shorthands and word boundaries, and the
   *        POSIX classes, the rest unchanged. Malformed text is copied as it is, for REAL to reject.
   * \param[in] pattern The pattern as RE2 reads it.
   * \param[in] icase   Whether the pattern starts case-insensitive (`Options::case_sensitive` false).
   * \return The pattern REAL reads the same way.
   */
  inline std::string translate_flavor(std::string_view pattern,
                                      bool             icase)
  {
    std::string       out;
    std::vector<bool> scopes   {icase}; // case folding per open group, innermost last
    bool              in_class {false};
    out.reserve(pattern.size() + 16U);
    const std::size_t n        {pattern.size()};
    for (std::size_t i {0}; i < n; ++i) {
      const char c {pattern[i]};
      if (c == '\\' && i + 1 < n) {
        const char  e         {pattern[i + 1]};
        const char  lower     {static_cast<char>(e | 0x20)};
        const bool  shorthand {lower == 'w' || lower == 'd' || lower == 's'};
        ascii_set   set;
        if (shorthand) {
          static_cast<void>(re2_ascii_set(std::string_view {&lower, 1}, set));
          const bool negated {e != lower};
          if (in_class) {
            if (negated) {
              append_complement(out, set, scopes.back());
            }
            else {
              append_set(out, set);
            }
          }
          else {
            out += negated ? "[^" : "[";
            append_set(out, set);
            out += ']';
          }
          ++i;
          continue;
        }
        if (!in_class && (e == 'b' || e == 'B')) {
          out += e == 'b' ? "(?a:\\b)" : "(?a:\\B)";
          ++i;
          continue;
        }
        if (e == 'Q') {
          // Literal text up to `\E`: nothing in it is syntax, so nothing in it is rewritten.
          const std::size_t end  {pattern.find("\\E", i + 2)};
          const std::size_t stop {end == std::string_view::npos ? n : end + 2};
          out.append(pattern.substr(i, stop - i));
          i = stop - 1;
          continue;
        }
        out += c;
        out += e;
        ++i;
        continue;
      }
      if (in_class) {
        if (c == '[' && i + 1 < n && pattern[i + 1] == ':') {
          const std::size_t close {pattern.find(":]", i + 2)};
          if (close != std::string_view::npos) {
            std::string_view name    {pattern.substr(i + 2, close - (i + 2))};
            const bool       negated {!name.empty() && name.front() == '^'};
            if (negated) {
              name.remove_prefix(1);
            }
            ascii_set set;
            if (re2_ascii_set(name, set) && name.size() > 1) {
              if (negated) {
                append_complement(out, set, scopes.back());
              }
              else {
                append_set(out, set);
              }
              i = close + 1;
              continue;
            }
          }
        }
        if (c == ']') {
          in_class = false;
        }
        out += c;
        continue;
      }
      if (c == '[') {
        in_class  = true;
        out      += c;
        if (i + 1 < n && pattern[i + 1] == '^') {
          out += '^';
          ++i;
        }
        if (i + 1 < n && pattern[i + 1] == ']') {
          out += "\\]"; // a leading `]` is a member
          ++i;
        }
        continue;
      }
      if (c == '(') {
        bool fold {scopes.back()};
        if (i + 1 < n && pattern[i + 1] == '?') {
          // `(?flags)` changes the enclosing scope, `(?flags:` opens one; `-` turns the flags after it off.
          std::size_t j          {i + 2};
          bool        on         {true};
          bool        flag_group {true};
          bool        next_fold  {fold};
          while (j < n && pattern[j] != ')' && pattern[j] != ':') {
            if (pattern[j] == '-') {
              on = false;
            }
            else if (pattern[j] == 'i') {
              next_fold = on;
            }
            else if (pattern[j] != 'm' && pattern[j] != 's' && pattern[j] != 'U') {
              flag_group = false;
              break;
            }
            ++j;
          }
          if (flag_group && j < n && pattern[j] == ')') {
            scopes.back() = next_fold; // no group opens: the `)` closing it pops nothing
            out          += "(?";
            out.append(pattern.substr(i + 2, j - (i + 2)));
            out          += ')';
            i             = j;
            continue;
          }
          scopes.push_back(flag_group && j < n ? next_fold : fold);
          out += c;
          continue;
        }
        scopes.push_back(fold);
        out += c;
        continue;
      }
      if (c == ')' && scopes.size() > 1) {
        scopes.pop_back();
      }
      out += c;
    }
    return out;
  }
} // namespace real::compat::re2::detail

#endif // REAL_RE2_FLAVOR_HPP
