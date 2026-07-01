/*!
 * \file ast.hpp
 * \brief Pattern text → AST, via a constexpr recursive-descent parser.
 *
 * The parser builds nodes in an index-based pool (no pointers, so it is
 * constexpr-friendly). It accepts only the syntax the rest of the pipeline
 * implements; everything else is a \ref real::regex_error.
 *
 * In code-point mode (the default), a character class carries specific non-ASCII
 * code-point members and ranges (`[é]`, `[à-ÿ]`, `[^é]`) alongside its ASCII
 * bitmap; they compile to the canonical UTF-8-ranges automaton, so a class matches
 * exactly those code points (and never an overlong / surrogate encoding). `.` and
 * an ASCII-only negated class (`[^x]`) still match any non-ASCII code point. In
 * bytes mode a non-ASCII class member is rejected (raw byte semantics). Every
 * construct consumes whole code points, so match boundaries never split a
 * sequence.
 */
#ifndef REAL_AST_HPP
#define REAL_AST_HPP

#include "version.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

#include "charclass.hpp"
#include "config.hpp"
#include "program.hpp"
#include "unicode_fold.hpp"
#include "utf8.hpp"

namespace real::detail {

  /*!
   * \brief Kind of an AST node; selects which fields of \ref real::detail::ast_node are meaningful.
   */
  enum class node_kind : std::uint8_t
  {
    empty,       //!< Matches the empty string.
    byte,        //!< One exact byte.
    klass,       //!< One codepoint constrained by classes[klass] (a \ref class_def; negated or not).
    any,         //!< One codepoint, except newline (the `.` metacharacter).
    concat,      //!< Children matched in sequence.
    repeat,      //!< Child repeated `[min, max]` times (max -1 = unbounded).
    alternation, //!< Children are branches, leftmost preferred.
    group,       //!< Child wrapped in a group; `group` >= 0 when capturing.
    anchor,      //!< Zero-width assertion; kind in \ref real::detail::ast_node::anchor.
    lookaround,  //!< Bounded lookaround: `child` = sub-pattern, `negated` = (?!/(?<!), `direction` = ahead/behind.
  };

  /*!
   * \brief The specific zero-width assertion of an `anchor` node (see `node_kind::anchor`).
   */
  enum class anchor_kind : std::uint8_t
  {
    caret,             //!< `^`  (text or line start, depending on multiline).
    dollar,            //!< `$`  (end, before a trailing `\n`, or line end with m).
    text_start,        //!< `\A`.
    text_end,          //!< `\Z`.
    word_boundary,     //!< `\b`.
    not_word_boundary, //!< `\B`.
    word_start,        //!< `\<` (start of word; REAL extension, not in Python re).
    word_end,          //!< `\>` (end of word; REAL extension, not in Python re).
  };

  /*!
   * \brief One AST node. Active fields depend on \ref kind (noted per field).
   */
  struct ast_node
  {
    node_kind    kind      {node_kind::empty};   //!< Which fields below are meaningful.
    std::uint8_t byte      {};                   //!< byte: the exact byte value.
    anchor_kind  anchor    {anchor_kind::caret}; //!< anchor: the assertion kind.
    bool         negated   {};                   //!< klass: written as `[^...]` / `\D` `\W` `\S`.
    bool         lazy      {};                   //!< repeat: prefer the shortest expansion.
    look_dir     direction {look_dir::ahead};    //!< lookaround: ahead `(?=`/`(?!` or behind `(?<=`/`(?<!`.
    std::int32_t klass     {-1};                 //!< klass: index into \ref ast::classes.
    std::int32_t min       {};                   //!< repeat: minimum count.
    std::int32_t max       {-1};                 //!< repeat: maximum count (-1 = unbounded).
    std::int32_t group     {-1};                 //!< group: capture number, -1 for `(?:...)`.
    std::int32_t child     {-1};                 //!< First child (concat, repeat, alternation, group).
    std::int32_t next      {-1};                 //!< Next sibling in the parent's child list.
  };

  //! \brief An inclusive code-point range `[lo, hi]` (a non-ASCII character class member, `lo >= 0x80`).
  struct code_range
  {
    std::uint32_t lo {}; //!< First code point (inclusive).
    std::uint32_t hi {}; //!< Last code point (inclusive).
  };

  //! \brief A parsed character class: its ASCII bitmap plus any non-ASCII code-point ranges. Bundling
  //!        the two (rather than parallel side tables) makes them impossible to desynchronize.
  struct class_def
  {
    char_class              ascii;  //!< ASCII members as a bitmap (all 256 bytes in bytes mode); pre-negation.
    std::vector<code_range> ranges; //!< Non-ASCII code-point ranges (code-point mode only; empty otherwise).
  };

  /*!
   * \brief A parsed pattern: the node pool plus side tables.
   *
   * Resource caps used during parsing and later Thompson unrolling are
   * centralized in config.hpp (\ref max_repeat_count, \ref max_group_count,
   * \ref max_nesting_depth, \ref max_program_size).
   */
  struct ast
  {
    std::vector<ast_node>    nodes;                      //!< The node pool; \ref root indexes it.
    std::vector<class_def>   classes;                    //!< Character classes as written, before negation.
    std::vector<named_group> names;                      //!< Named capture groups.
    flags                    inline_flags {flags::none}; //!< Flags from a leading `(?ims)`.
    std::int32_t             group_count  {};            //!< Number of capturing groups.
    std::int32_t             root         {-1};          //!< Index of the root node.
  };

  //! \brief What a `\<digit>` escape decoded to (see decode_digit_escape()).
  enum class digit_escape_kind : std::uint8_t
  {
    octal,          //!< An octal byte escape; `value` is the byte (0-255).
    group_ref,      //!< A decimal group number; `value` is the group (a back-reference in a pattern).
    octal_overflow, //!< A 3-octal-digit escape greater than 0o377 (an error in CPython).
  };

  //! \brief Result of decode_digit_escape().
  struct digit_escape_result
  {
    digit_escape_kind kind   {digit_escape_kind::group_ref}; //!< Which interpretation applies.
    unsigned          value  {};                             //!< Octal byte, or decimal group number.
    std::size_t       length {};                             //!< Characters consumed from the first digit.
  };

  /*!
   * \brief Decodes a `\<digit>` escape per CPython's exact rule (shared by the pattern parser
   *        and the replacement-template parser, so the two never drift).
   *
   * \p first indexes the first digit (just past the backslash). A `\0` prefix, or any 1-7
   * digit immediately followed by two more octal digits, is an OCTAL escape (`\0`: value & 0xff;
   * the 3-octal form errors above 0o377). Otherwise the digits are a decimal group number — a
   * back-reference in a pattern, a group reference in a replacement template.
   *
   * \param[in] text  The pattern or template text.
   * \param[in] first Offset of the first digit.
   * \return The decoded kind, value and consumed length.
   */
  constexpr digit_escape_result decode_digit_escape(std::string_view text,
                                                    std::size_t      first)
  {
    const auto is_octal {[](char c) { return c >= '0' && c <= '7'; }};
    const auto is_digit {[](char c) { return c >= '0' && c <= '9'; }};
    if (text[first] == '0') {
      unsigned    value {};
      std::size_t taken {};
      while (taken < 3 && first + taken < text.size() && is_octal(text[first + taken])) {
        value = (value * 8U) + static_cast<unsigned>(text[first + taken] - '0');
        ++taken;
      }
      return {.kind = digit_escape_kind::octal, .value = value & 0xFFU, .length = taken};
    }
    std::size_t length {1};
    if (first + 1 < text.size() && is_digit(text[first + 1])) {
      length = 2;
      if (is_octal(text[first]) && is_octal(text[first + 1]) && first + 2 < text.size() &&
          is_octal(text[first + 2])) {
        const unsigned value {(static_cast<unsigned>(text[first] - '0') * 8U * 8U) +
                              (static_cast<unsigned>(text[first + 1] - '0') * 8U) +
                              static_cast<unsigned>(text[first + 2] - '0')};
        return value > 0xFFU ? digit_escape_result {.kind   = digit_escape_kind::octal_overflow,
                                                    .value  = value,
                                                    .length = 3}
                             : digit_escape_result {.kind   = digit_escape_kind::octal, .value = value, .length = 3};
      }
    }
    unsigned group {};
    for (std::size_t k = 0; k < length; ++k) {
      group = (group * 10U) + static_cast<unsigned>(text[first + k] - '0');
    }
    return {.kind = digit_escape_kind::group_ref, .value = group, .length = length};
  }

  /*!
   * \brief Recursive-descent parser: a pattern string in, an \ref ast out.
   */
  class parser
  {
  public:

    /*!
     * \brief Binds the parser to a pattern and the constructor flags.
     * \param[in] pattern      The pattern text (borrowed, must outlive use).
     * \param[in] initial_flags Flags from the constructor; only `verbose` affects
     *                          parsing (a leading `(?x)` can add it too).
     */
    constexpr explicit parser(std::string_view pattern,
                              flags            initial_flags = flags::none)
      : pattern_(pattern),
        verbose_(has_flag(initial_flags, flags::verbose)),
        bytes_(has_flag(initial_flags, flags::bytes)),
        ecma_(has_flag(initial_flags, flags::ecma)),
        icase_(has_flag(initial_flags, flags::icase))
    {}

    /*!
     * \brief Parses the whole pattern.
     * \return The resulting \ref ast.
     * \throws real::regex_error on any unsupported or malformed syntax.
     */
    constexpr ast parse()
    {
      ast out;
      while (parse_global_flags_prefix(out)) {}
      out.root = parse_alternation(out);
      if (pos_ != pattern_.size()) {
        fail("unbalanced parenthesis"); // only a stray ')' stops earlier
      }
      return out;
    }

  private:

    std::string_view pattern_;          //!< The pattern being parsed.
    std::size_t      pos_           {}; //!< Current read offset into \ref pattern_.
    std::int32_t     depth_         {}; //!< Current group nesting (see \ref max_nesting_depth).
    bool             verbose_       {}; //!< `re.X`: skip unescaped whitespace and `#` comments outside classes.
    bool             in_lookaround_ {}; //!< True while parsing a lookaround sub-pattern (rejects nesting).
    bool             bytes_         {}; //!< In \ref flags::bytes mode, rejects code-point escapes (`\u`/`\U`).
    bool             ecma_          {}; //!< ECMAScript grammar: `\A \Z \< \>` are identity-escape literals, not anchors.
    bool             icase_         {}; //!< `re.I`: a cased literal is promoted to a foldable singleton class.

    /*!
     * \brief In verbose mode, consumes insignificant whitespace and `#` comments.
     *
     * No-op unless \ref verbose_. Called only between tokens outside character
     * classes; escaped whitespace (`\ `) is read as a literal by the escape
     * parser, never reaching here.
     */
    constexpr void skip_insignificant()
    {
      if (!verbose_) {
        return;
      }
      while (!eof()) {
        const char ch {peek()};
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v') {
          ++pos_;
        }
        else if (ch == '#') {
          while (!eof() && peek() != '\n') {
            ++pos_;
          }
        }
        else {
          break;
        }
      }
    }

    /*!
     * \brief Aborts the parse with a \ref real::regex_error at the current offset.
     *
     * A template so the always-throwing body stays legal inside a constexpr
     * function (the ill-formed, no-diagnostic-required rule does not apply to
     * templates); during constant evaluation the throw fails compilation with
     * \p message in the diagnostic trace.
     *
     * \tparam Error The exception type to throw (defaults to regex_error).
     * \param[in] message The cause, shown in the error and the constexpr trace.
     */
    template <typename Error = regex_error>
    [[noreturn]] constexpr void fail(const char* message) const
    {
      throw Error(message, pos_);
    }

    /*!
     * \brief Returns `true` if the read offset is at or past the end of the pattern.
     */
    [[nodiscard]] constexpr bool eof() const
    {
      return pos_ >= pattern_.size();
    }

    /*!
     * \brief Returns the current character without consuming it (undefined at eof()).
     */
    [[nodiscard]] constexpr char peek() const
    {
      return pattern_[pos_];
    }

    /*!
     * \brief Consumes the current character if it equals \p ch.
     * \param[in] ch The character to match.
     * \return `true` (and advances) on a match, else `false`.
     */
    [[nodiscard]] constexpr bool accept(char ch)
    {
      if (!eof() && peek() == ch) {
        ++pos_;
        return true;
      }
      return false;
    }

    /*!
     * \brief Returns `true` if \p ch is in `[0-9A-Za-z]`.
     * \param[in] ch A character.
     * \return `true` if \p ch is in `[0-9A-Za-z]`.
     */
    static constexpr bool is_ascii_alnum(char ch)
    {
      return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    }

    /*!
     * \brief Appends \p node to the pool.
     * \param[in,out] out  The AST being built.
     * \param[in]     node The node to append.
     * \return The index of the appended node.
     */
    static constexpr std::int32_t add_node(ast&     out,
                                           ast_node node)
    {
      out.nodes.push_back(node);
      return static_cast<std::int32_t>(out.nodes.size()) - 1;
    }

    /*!
     * \brief Interns a class bitmap and appends a \ref node_kind::klass node.
     * \param[in,out] out     The AST being built.
     * \param[in]     klass      The class bitmap as written (before negation).
     * \param[in]     negated Whether the class was written negated.
     * \param[in]     ranges  Non-ASCII code-point ranges of the class (code-point mode; empty otherwise).
     * \return The index of the new node.
     */
    static constexpr std::int32_t add_class_node(ast&                           out,
                                                 const char_class&              klass,
                                                 bool                           negated,
                                                 const std::vector<code_range>& ranges = {})
    {
      out.classes.push_back({.ascii = klass, .ranges = ranges});
      const auto index {static_cast<std::int32_t>(out.classes.size()) - 1};
      return add_node(out, {.kind = node_kind::klass, .negated = negated, .klass = index});
    }

    /*!
     * \brief Parses `alternation := sequence ('|' sequence)*`.
     *
     * The leftmost branch is preferred (Python / Perl semantics, not longest).
     *
     * \param[in,out] out The AST being built.
     * \return The index of the resulting node (a branch, or a bare sequence).
     */
    constexpr std::int32_t parse_alternation(ast& out)
    {
      const std::int32_t first {parse_sequence(out)};
      if (eof() || peek() != '|') {
        return first;
      }
      std::int32_t last {first};
      while (accept('|')) {
        const std::int32_t branch {parse_sequence(out)}; // may be empty
        out.nodes[static_cast<std::size_t>(last)].next = branch;
        last                                           = branch;
      }
      const std::int32_t alt                         = add_node(out, {.kind = node_kind::alternation});
      out.nodes[static_cast<std::size_t>(alt)].child = first;
      return alt;
    }

    /*!
     * \brief Parses `sequence := (atom quantifier?)*`, stopping at `|` or `)`.
     * \param[in,out] out The AST being built.
     * \return The index of a concat node, a single atom, or an empty node.
     */
    constexpr std::int32_t parse_sequence(ast& out)
    {
      std::int32_t first {-1};
      std::int32_t last  {-1};
      while (true) {
        skip_insignificant(); // verbose: between elements and before '|' / ')'
        if (eof() || peek() == '|' || peek() == ')') {
          break;
        }
        std::int32_t atom {parse_atom(out)};
        skip_insignificant(); // verbose: whitespace between an atom and its quantifier
        atom = parse_quantifier(out, atom);
        if (first == -1) {
          first = atom;
        }
        else {
          out.nodes[static_cast<std::size_t>(last)].next = atom;
        }
        last = atom;
      }
      if (first == -1) {
        return add_node(out, {.kind = node_kind::empty});
      }
      if (out.nodes[static_cast<std::size_t>(first)].next == -1) {
        return first; // single atom: no concat wrapper needed
      }
      const std::int32_t seq                         = add_node(out, {.kind = node_kind::concat});
      out.nodes[static_cast<std::size_t>(seq)].child = first;
      return seq;
    }

    /*!
     * \brief Parses one atom: a literal, `.`, a class, a group, an anchor or an escape.
     * \param[in,out] out The AST being built.
     * \return The index of the atom node.
     */
    constexpr std::int32_t parse_atom(ast& out)
    {
      const char ch {peek()};
      switch (ch) {
        case '*':
        case '+':
        case '?':
          fail("nothing to repeat");
        case '^':
          ++pos_;
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::caret});
        case '$':
          ++pos_;
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::dollar});
        case '(':
          return parse_group(out);
        case ')':
          fail("unbalanced parenthesis");
        case '.':
          ++pos_;
          return add_node(out, {.kind = node_kind::any});
        case '[':
          return parse_class(out);
        case '\\':
          return parse_escape(out);
        default:
          {
            // In code-point mode a raw non-ASCII byte begins a UTF-8 sequence: decode the WHOLE
            // code point and emit it as one atom (the same emission as `\uHHHH`), so a following
            // quantifier applies to the code point, not just its last byte (the é+ bug). A malformed
            // sequence is a pattern error, not a silent literal. In bytes mode, and for ASCII, a raw
            // byte stays a single byte node (so the compat layer's bytes|ecma path is unchanged).
            if (!bytes_ && static_cast<std::uint8_t>(ch) >= 0x80U) {
              const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, pos_)};
              if (!decoded.valid) {
                fail("invalid UTF-8 byte in pattern");
              }
              pos_ += decoded.length;
              return emit_literal_codepoint(out, static_cast<std::int32_t>(decoded.cp));
            }
            // Like Python: lone '{', ']' and '}' are ordinary characters.
            ++pos_;
            return emit_literal_codepoint(out, static_cast<std::uint8_t>(ch));
          }
      }
    }

    /*!
     * \brief Wraps \p atom in a repeat node if a quantifier follows.
     *
     * Grammar: `quantifier := ('*' | '+' | '?' | '{n}' | '{n,}' | '{,m}' |
     * '{n,m}') '?'?`. An invalid `{...}` is not a quantifier at all
     * and stays literal text, exactly like Python (e.g. `a{`, `a{2,3x`,
     * `a{,}` all match literally). A bare anchor cannot be repeated.
     *
     * \param[in,out] out  The AST being built.
     * \param[in]     atom Index of the atom the quantifier would apply to.
     * \return The repeat node index, or \p atom unchanged if no quantifier.
     */
    constexpr std::int32_t parse_quantifier(ast&         out,
                                            std::int32_t atom)
    {
      if (eof()) {
        return atom;
      }
      // Like Python: a bare anchor cannot be repeated ((?:^)* is fine).
      if (out.nodes[static_cast<std::size_t>(atom)].kind == node_kind::anchor &&
          (peek() == '*' || peek() == '+' || peek() == '?' || peek() == '{')) {
        std::int32_t ignored_min {};
        std::int32_t ignored_max {-1};
        if (peek() != '{' || try_parse_braces(ignored_min, ignored_max)) {
          fail("nothing to repeat");
        }
      }
      std::int32_t min {};
      std::int32_t max {-1};
      switch (peek()) {
        case '*':
          ++pos_;
          break;
        case '+':
          ++pos_;
          min = 1;
          break;
        case '?':
          ++pos_;
          max = 1;
          break;
        case '{':
          if (!try_parse_braces(min, max)) {
            return atom; // literal '{': handled as the next atom
          }
          break;
        default:
          return atom;
      }
      const bool lazy {accept('?')};
      if (!eof()) {
        const char   ch          {peek()};
        std::int32_t ignored_min {};
        std::int32_t ignored_max {-1};
        if (ch == '*' || ch == '+' || ch == '?' ||
            (ch == '{' && try_parse_braces(ignored_min, ignored_max))) {
          fail("multiple repeat");
        }
      }
      return add_node(out, {.kind = node_kind::repeat, .lazy = lazy, .min = min, .max = max, .child = atom});
    }

    /*!
     * \brief Tries to parse `{n} / {n,} / {,m} / {n,m}` starting at `{`.
     * \param[out] min Lower bound on success.
     * \param[out] max Upper bound on success (-1 for unbounded).
     * \return `true` on a valid quantifier (position advanced); `false` if the
     *         braces are not a quantifier (position restored — literal text).
     * \throws real::regex_error when the bounds are impossible (min > max).
     */
    constexpr bool try_parse_braces(std::int32_t& min,
                                    std::int32_t& max)
    {
      const std::size_t saved_pos   {pos_};
      ++pos_; // consume '{'
      const std::int32_t repeat_min {parse_repeat_count()};
      std::int32_t       repeat_max {repeat_min};
      bool               has_comma  {};
      if (accept(',')) {
        has_comma  = true;
        repeat_max = parse_repeat_count();
      }
      if (!accept('}') || (repeat_min < 0 && repeat_max < 0)) {
        pos_ = saved_pos;
        return false; // "{", "{}", "{,}", "{x"…: literal text
      }
      min = repeat_min < 0 ? 0 : repeat_min;
      max = (has_comma && repeat_max < 0) ? -1 : repeat_max;
      if (max != -1 && max < min) {
        pos_ = saved_pos;
        fail("min repeat greater than max repeat");
      }
      return true;
    }

    /*!
     * \brief Reads an optional decimal repeat count.
     * \return The count, or -1 when no digits are present.
     * \throws real::regex_error if the count exceeds \ref max_repeat_count
     *         (counted repetitions are compiled by unrolling, so they are capped).
     */
    constexpr std::int32_t parse_repeat_count()
    {
      std::int32_t value {-1};
      while (!eof() && peek() >= '0' && peek() <= '9') {
        value = value < 0 ? 0 : value;
        value = (value * 10) + (peek() - '0');
        if (value > max_repeat_count) {
          fail("repetition count too large");
        }
        ++pos_;
      }
      return value;
    }

    /*!
     * \brief Consumes \p ch or fails.
     * \param[in] ch      The required character.
     * \param[in] message Error message if \p ch is not present.
     * \throws real::regex_error when the next character is not \p ch.
     */
    constexpr void expect(char        ch,
                          const char* message)
    {
      if (!accept(ch)) {
        fail(message);
      }
    }

    /*!
     * \brief Maps a flag letter to its \ref flags value.
     * \param[in] letter One of 'i', 'm', 's', 'a'.
     * \return The flag; \ref flags::none for 'a' (ASCII — already the default)
     *         and for any unrecognized letter.
     */
    static constexpr flags flag_for_letter(char letter)
    {
      switch (letter) {
        case 'i':
          return flags::icase;
        case 'm':
          return flags::multiline;
        case 's':
          return flags::dotall;
        case 'x':
          return flags::verbose;
        // 'a' (ASCII) is a recognized flag, accepted as a no-op because ASCII
        // is already this library's semantics — intent distinct from an
        // unrecognized letter, hence kept separate from default.
        case 'a': // NOLINT(bugprone-branch-clone)
          return flags::none;
        default:
          return flags::none;
      }
    }

    /*!
     * \brief Returns `true` if \p letter is a flag letter (imsax).
     * \param[in] letter A character.
     * \return `true` if \p letter is a flag letter (imsax).
     */
    static constexpr bool is_flag_letter(char letter)
    {
      return letter == 'i' || letter == 'm' || letter == 's' || letter == 'a' || letter == 'x';
    }

    /*!
     * \brief Consumes a leading `(?ims)` global-flags group, if present.
     *
     * Like Python (3.11+), global flags are only legal at the very start of the
     * pattern; later occurrences are rejected in \ref parse_group.
     *
     * \param[in,out] out Receives the flags into \ref ast::inline_flags.
     * \return `true` if a flags group was consumed (position advanced), else
     *         `false` (position restored, for \ref parse_group to handle).
     */
    constexpr bool parse_global_flags_prefix(ast& out)
    {
      const std::size_t saved_pos {pos_};
      if (!accept('(') || !accept('?')) {
        pos_ = saved_pos;
        return false;
      }
      flags found      {flags::none};
      bool  any_letter {};
      while (!eof() && is_flag_letter(peek())) {
        found      = found | flag_for_letter(peek());
        any_letter = true;
        ++pos_;
      }
      if (!any_letter || !accept(')')) {
        pos_ = saved_pos; // some other (?...) construct: let parse_group decide
        return false;
      }
      out.inline_flags = out.inline_flags | found;
      if (has_flag(found, flags::verbose)) {
        verbose_ = true; // affects how the rest of the pattern is parsed
      }
      if (has_flag(found, flags::icase)) {
        icase_ = true;   // a leading (?i) makes cased literals foldable, like the constructor flag
      }
      return true;
    }

    /*!
     * \brief Parses a group construct.
     *
     * Grammar:
     * \code
     * group := '(' alternation ')'           capturing, numbered by '('
     *        | '(?:' alternation ')'         non-capturing
     *        | '(?P<name>' alternation ')'   named (Python style)
     *        | '(?<name>'  alternation ')'   named (.NET style)
     * \endcode
     * Unsupported extensions (lookaround, backreferences, atomic groups,
     * scoped inline flags) fail with a message naming the feature. Nesting
     * beyond \ref max_nesting_depth is rejected.
     *
     * \param[in,out] out The AST being built.
     * \return The index of the \ref node_kind::group node.
     * \throws real::regex_error on an unterminated or unsupported group.
     */
    constexpr std::int32_t parse_group(ast& out)
    {
      const std::size_t open_pos {pos_};
      if (++depth_ > max_nesting_depth) {
        fail("pattern nesting too deep");
      }
      ++pos_; // consume '('
      std::int32_t group {-1};
      if (accept('?')) {
        if (accept('#')) {
          // (?#...) comment: skip to the first ')' (a backslash is not special here, like re);
          // emits nothing. Works the same in verbose and non-verbose mode.
          while (!eof() && peek() != ')') {
            ++pos_;
          }
          if (!accept(')')) {
            pos_ = open_pos;
            fail("missing ), unterminated comment");
          }
          --depth_;
          return add_node(out, {.kind = node_kind::empty});
        }
        if (accept(':')) {
          // non-capturing
        }
        else if (accept('P')) {
          if (accept('<')) {
            group = new_group(out, open_pos);
            parse_group_name(out, group);
          }
          else if (!eof() && peek() == '=') {
            fail("named backreferences are not supported");
          }
          else {
            fail("unknown extension");
          }
        }
        else if (accept('<')) {
          if (!eof() && (peek() == '=' || peek() == '!')) {
            return parse_lookaround(out, look_dir::behind, open_pos);
          }
          group = new_group(out, open_pos);
          parse_group_name(out, group);
        }
        else if (!eof() && (peek() == '=' || peek() == '!')) {
          return parse_lookaround(out, look_dir::ahead, open_pos);
        }
        else if (!eof() && peek() == '>') {
          fail("atomic groups are not supported");
        }
        else if (!eof() && peek() == '(') {
          fail("conditional groups are not supported");
        }
        else if (!eof() && is_flag_letter(peek())) {
          while (!eof() && is_flag_letter(peek())) {
            ++pos_;
          }
          if (!eof() && peek() == ':') {
            fail("scoped inline flags are not supported");
          }
          fail("global flags not at the start of the expression");
        }
        else {
          fail("unknown extension");
        }
      }
      else {
        group = new_group(out, open_pos);
      }
      const std::int32_t body {parse_alternation(out)};
      if (!accept(')')) {
        pos_ = open_pos;
        fail("missing ), unterminated subpattern");
      }
      --depth_;
      return add_node(out, {.kind = node_kind::group, .group = group, .child = body});
    }

    /*!
     * \brief Parses a lookaround after `(?=` / `(?!` (ahead) or `(?<=` / `(?<!` (behind) —
     *        the `=`/`!` is not yet consumed.
     *
     * Builds a \ref node_kind::lookaround node. The sub-pattern is a full alternation; its
     * capture groups advance the global group counter (so outer group numbers stay
     * consistent) but are compiled capture-free (V1 limitation, documented). Nesting a
     * lookaround inside a lookaround is rejected. Boundedness and the byte L_max are
     * enforced later by the compiler.
     *
     * \param[in,out] out       The AST being built.
     * \param[in]     direction Ahead or behind.
     * \param[in]     open_pos  Offset of the group's `(` (for error reporting).
     * \return The index of the lookaround node.
     */
    constexpr std::int32_t parse_lookaround(ast&        out,
                                            look_dir    direction,
                                            std::size_t open_pos)
    {
      if (in_lookaround_) {
        fail("nested lookaround is not supported");
      }
      const bool negative {peek() == '!'};
      ++pos_; // consume '=' or '!'
      in_lookaround_         = true;
      const std::int32_t sub {parse_alternation(out)};
      in_lookaround_         = false;
      if (!accept(')')) {
        pos_ = open_pos;
        fail("missing ), unterminated subpattern");
      }
      --depth_;
      return add_node(out, {.kind      = node_kind::lookaround,
                            .negated   = negative,
                            .direction = direction,
                            .child     = sub});
    }

    /*!
     * \brief Allocates the next capture group number.
     * \param[in,out] out      The AST being built.
     * \param[in]     open_pos Offset of the group's `(` (for error reporting).
     * \return The new (1-based) capture group number.
     * \throws real::regex_error beyond \ref max_group_count.
     */
    constexpr std::int32_t new_group(ast&        out,
                                     std::size_t open_pos)
    {
      if (out.group_count >= max_group_count) {
        pos_ = open_pos;
        fail("too many capture groups");
      }
      return ++out.group_count;
    }

    /*!
     * \brief Returns `true` if \p ch may start a group name.
     * \param[in] ch A character.
     * \return `true` if \p ch may start a group name.
     */
    static constexpr bool is_name_start(char ch)
    {
      return ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    }

    /*!
     * \brief Parses `name := [A-Za-z_][A-Za-z0-9_]* '>'` and records it.
     * \param[in,out] out   The AST; the name is appended to \ref ast::names.
     * \param[in]     group The capture number this name refers to.
     * \throws real::regex_error on a bad character or a duplicate name.
     */
    constexpr void parse_group_name(ast&         out,
                                    std::int32_t group)
    {
      const std::size_t begin {pos_};
      if (eof() || !is_name_start(peek())) {
        fail("bad character in group name");
      }
      while (!eof() && (is_ascii_alnum(peek()) || peek() == '_')) {
        ++pos_;
      }
      const std::size_t end {pos_};
      expect('>', "bad character in group name");
      for (const named_group& existing : out.names) {
        const std::string_view name    {pattern_.substr(begin, end - begin)};
        const auto             e_begin {static_cast<std::size_t>(existing.begin)};
        const auto             e_end   {static_cast<std::size_t>(existing.end)};
        if (pattern_.substr(e_begin, e_end - e_begin) == name) {
          fail("redefinition of group name");
        }
      }
      out.names.push_back({.group = group,
                           .begin = static_cast<std::int32_t>(begin),
                           .end   = static_cast<std::int32_t>(end)});
    }

    /*!
     * \brief Parses a single-byte escape (valid inside and outside classes).
     *
     * Handles `\n` `\t` `\r` `\f` `\v` `\a` `\0`, `\xHH` and
     * escaped ASCII punctuation.
     *
     * \return The byte value, or -1 when the escape is not a single byte
     *         (the caller then handles `\d` `\w` `\s`, etc.).
     * \throws real::regex_error on a malformed `\x` escape.
     */
    constexpr std::int32_t parse_byte_escape()
    {
      const char ch {peek()};
      if (ch >= '0' && ch <= '9') {
        return parse_digit_escape(); // octal byte, or a rejected back-reference
      }
      switch (ch) {
        case 'n':
          ++pos_;
          return '\n';
        case 't':
          ++pos_;
          return '\t';
        case 'r':
          ++pos_;
          return '\r';
        case 'f':
          ++pos_;
          return '\f';
        case 'v':
          ++pos_;
          return '\v';
        case 'a':
          ++pos_;
          // REAL/Python `\a` is the bell (0x07). ECMAScript has no `\a` escape — it is an identity
          // escape (the literal 'a'). Gate under ecma; `\n \t \r \f \v` are ECMAScript ControlEscapes
          // and stay unchanged. This covers both contexts (parse_byte_escape is shared with classes).
          if (ecma_) { return 'a'; }
          return '\a';
        case 'x':
          {
            ++pos_;
            const std::int32_t high_nibble {hex_digit()};
            const std::int32_t low_nibble  {hex_digit()};
            return (high_nibble * 16) + low_nibble; // arithmetic, not signed bitwise (MISRA)
          }
        default:
          // Any escaped ASCII punctuation is that literal character.
          if (static_cast<std::uint8_t>(ch) < 0x80 && !is_ascii_alnum(ch)) {
            ++pos_;
            return static_cast<std::uint8_t>(ch);
          }
          return -1;
      }
    }

    /*!
     * \brief Parses a `\<digit>` escape via the shared decode_digit_escape().
     *
     * Octal escapes (`\0`, `\012`, a three-octal-digit run) become one byte (value & 0xff,
     * mirroring `\xHH`). A decimal group number is a back-reference, which REAL does not
     * support (a deliberate, documented limitation).
     *
     * \return The byte value of an octal escape.
     * \throws real::regex_error on an over-long octal escape or a back-reference.
     */
    constexpr std::int32_t parse_digit_escape()
    {
      const digit_escape_result decoded {decode_digit_escape(pattern_, pos_)};
      pos_ += decoded.length;
      if (decoded.kind == digit_escape_kind::octal) {
        return static_cast<std::int32_t>(decoded.value); // a single byte, like \xHH
      }
      if (decoded.kind == digit_escape_kind::octal_overflow) {
        fail("octal escape value outside of range 0-0o377");
      }
      fail("backreferences are not supported"); // a decimal group number = a back-reference
    }

    /*!
     * \brief Consumes one hexadecimal digit.
     * \return Its value in `[0, 15]`.
     * \throws real::regex_error if the next character is not a hex digit.
     */
    constexpr std::int32_t hex_digit()
    {
      if (eof()) {
        fail("invalid \\x escape: expected two hex digits");
      }
      const char ch {peek()};
      ++pos_;
      if (ch >= '0' && ch <= '9') {
        return ch - '0';
      }
      if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
      }
      if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
      }
      --pos_;
      fail("invalid \\x escape: expected two hex digits");
    }

    /*!
     * \brief Decodes a `\uHHHH` (4 hex) or `\UHHHHHHHH` (8 hex) code-point escape (str only).
     *
     * Rejected with clear messages: byte mode (no code-point meaning), a surrogate
     * (U+D800–U+DFFF), beyond U+10FFFF, or incomplete hex. The backslash and `u`/`U` are
     * already consumed; this reads the hex digits.
     *
     * \param[in] capital True for `\U` (8 digits), false for `\u` (4 digits).
     * \return The code point in `[0, 0x10FFFF]` (never a surrogate).
     */
    constexpr std::int32_t parse_unicode_codepoint(bool capital)
    {
      if (bytes_) {
        fail("\\u and \\U escapes are not allowed in bytes patterns");
      }
      const int    width {capital ? 8 : 4};
      std::int32_t value {};
      for (int i = 0; i < width; ++i) {
        std::int32_t digit {-1};
        if (!eof()) {
          const char ch {peek()};
          if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
          }
          else if (ch >= 'a' && ch <= 'f') {
            digit = (ch - 'a') + 10;
          }
          else if (ch >= 'A' && ch <= 'F') {
            digit = (ch - 'A') + 10;
          }
        }
        if (digit < 0) {
          fail(capital ? "invalid \\U escape: expected 8 hex digits"
                       : "invalid \\u escape: expected 4 hex digits");
        }
        value = (value * 16) + digit;
        ++pos_;
      }
      if (value >= 0xD800 && value <= 0xDFFF) {
        fail("invalid Unicode escape: surrogate code point");
      }
      if (value > 0x10FFFF) {
        fail("invalid Unicode escape: code point out of range");
      }
      return value;
    }

    /*!
     * \brief Emits a code point as its 1–4 UTF-8 bytes — the same byte-level form a literal
     *        multi-byte character produces — as a single atom (a byte node, or a concat).
     * \param[in,out] out The AST being built.
     * \param[in]     cp  A code point in `[0, 0x10FFFF]`.
     * \return The node index.
     */
    constexpr std::int32_t emit_codepoint_utf8(ast&         out,
                                               std::int32_t cp)
    {
      const auto value {static_cast<std::uint32_t>(cp)};
      if (value < 0x80U) {
        return add_node(out, {.kind = node_kind::byte, .byte = static_cast<std::uint8_t>(value)});
      }
      std::int32_t first {-1};
      std::int32_t last  {-1};
      const auto   emit_byte {[&](std::uint8_t one) {
                                const std::int32_t node {add_node(out, {.kind = node_kind::byte, .byte = one})};
                                if (first < 0) {
                                  first = node;
                                }
                                else {
                                  out.nodes[static_cast<std::size_t>(last)].next = node;
                                }
                                last = node;
                              }};
      if (value < 0x800U) {
        emit_byte(static_cast<std::uint8_t>(0xC0U | (value >> 6U)));
        emit_byte(static_cast<std::uint8_t>(0x80U | (value & 0x3FU)));
      }
      else if (value < 0x10000U) {
        emit_byte(static_cast<std::uint8_t>(0xE0U | (value >> 12U)));
        emit_byte(static_cast<std::uint8_t>(0x80U | ((value >> 6U) & 0x3FU)));
        emit_byte(static_cast<std::uint8_t>(0x80U | (value & 0x3FU)));
      }
      else {
        emit_byte(static_cast<std::uint8_t>(0xF0U | (value >> 18U)));
        emit_byte(static_cast<std::uint8_t>(0x80U | ((value >> 12U) & 0x3FU)));
        emit_byte(static_cast<std::uint8_t>(0x80U | ((value >> 6U) & 0x3FU)));
        emit_byte(static_cast<std::uint8_t>(0x80U | (value & 0x3FU)));
      }
      const std::int32_t seq {add_node(out, {.kind = node_kind::concat})};
      out.nodes[static_cast<std::size_t>(seq)].child = first;
      return seq;
    }

    /*!
     * \brief Emits a code-point *literal* (code-point provenance: a raw character or `\\u`/`\\U`).
     *
     * Under `icase`, a CASED literal is promoted to a foldable singleton class so the compiler folds
     * it to its whole case orbit (`k`↦`{k, K, Kelvin}`, `é`↦`{é, É}`). An ASCII letter folds in any
     * mode; a non-ASCII code point folds only in text mode (a bytes class carries no ranges). A
     * non-cased literal, or no `icase`, keeps the zero-overhead byte / UTF-8 path. `\\xHH` has byte
     * provenance and never routes here, so it is never folded — the deliberate provenance split.
     */
    constexpr std::int32_t emit_literal_codepoint(ast&         out,
                                                  std::int32_t cp)
    {
      if (icase_) {
        const bool ascii_letter {(cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')};
        if (ascii_letter) {
          char_class bitmap;
          bitmap.set(static_cast<std::uint8_t>(cp));
          return add_class_node(out, bitmap, false);
        }
        if (!bytes_ && cp >= 0x80 &&
            detail::find_fold_index(static_cast<std::uint32_t>(cp)) != detail::unicode_fold_table_size) {
          const std::vector<code_range> single {
            {.lo = static_cast<std::uint32_t>(cp), .hi = static_cast<std::uint32_t>(cp)}};
          return add_class_node(out, char_class {}, false, single);
        }
      }
      // Not promoted: a raw byte (ASCII, or any byte in bytes mode) is a byte node; a non-ASCII
      // code point in text mode is emitted as its UTF-8 bytes.
      if (bytes_ || cp < 0x80) {
        return add_node(out, {.kind = node_kind::byte, .byte = static_cast<std::uint8_t>(cp)});
      }
      return emit_codepoint_utf8(out, cp);
    }

    /*!
     * \brief Parses an escape outside a character class.
     *
     * Handles the class escapes `\d` `\D` `\w` `\W` `\s` `\S`, the
     * anchors `\A` `\Z` `\b` `\B`, and single-byte escapes.
     *
     * \param[in,out] out The AST being built.
     * \return The index of the resulting node.
     * \throws real::regex_error on a dangling or unsupported escape.
     */
    constexpr std::int32_t parse_escape(ast& out)
    {
      ++pos_; // consume the backslash
      if (eof()) {
        fail("dangling backslash");
      }
      switch (peek()) {
        case 'd':
          ++pos_;
          return add_class_node(out, digit_set(), false);
        case 'D':
          ++pos_;
          return add_class_node(out, digit_set(), true);
        case 'w':
          ++pos_;
          return add_class_node(out, word_set(), false);
        case 'W':
          ++pos_;
          return add_class_node(out, word_set(), true);
        case 's':
          ++pos_;
          return add_class_node(out, space_set(), false);
        case 'S':
          ++pos_;
          return add_class_node(out, space_set(), true);
        // `\A \Z \< \>` are REAL extensions (text-start/end, word-start/end). ECMAScript has no
        // such escapes — they are identity escapes (the literal character). Under the ecma flag
        // (the std-compat layer), emit the literal; otherwise keep REAL's anchor. `\b`/`\B` are
        // standard word boundaries in both and stay unchanged.
        case 'A':
          ++pos_;
          // ecma: `\A` is the literal 'A' (Annex B identity escape); a cased letter, so it folds under icase.
          if (ecma_) {
            return emit_literal_codepoint(out, 'A');
          }
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::text_start});
        case 'Z':
          ++pos_;
          if (ecma_) {
            return emit_literal_codepoint(out, 'Z'); // ecma: literal 'Z', folds under icase
          }
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::text_end});
        case 'b':
          ++pos_;
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::word_boundary});
        case 'B':
          ++pos_;
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::not_word_boundary});
        case '<':
          ++pos_;
          if (ecma_) {
            return emit_literal_codepoint(out, '<'); // ecma: literal '<' (non-cased -> a plain byte)
          }
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::word_start});
        case '>':
          ++pos_;
          if (ecma_) {
            return emit_literal_codepoint(out, '>'); // ecma: literal '>'
          }
          return add_node(out, {.kind = node_kind::anchor, .anchor = anchor_kind::word_end});
        case 'u':
          ++pos_;
          return emit_literal_codepoint(out, parse_unicode_codepoint(false));
        case 'U':
          ++pos_;
          return emit_literal_codepoint(out, parse_unicode_codepoint(true));
        case 'N':
          fail("named Unicode escapes (\\N{...}) are not supported");
        default:
          {
            const std::int32_t byte_value {parse_byte_escape()};
            if (byte_value < 0) {
              fail("unsupported escape sequence");
            }
            // A `\xHH` / octal escape with value < 0x80 is an ASCII character (byte == code point): a
            // cased one folds under icase like a raw ASCII literal (`\x4B` == `K`). A value >= 0x80
            // keeps byte provenance and is never folded — the documented text-mode divergence.
            if (byte_value < 0x80) {
              return emit_literal_codepoint(out, byte_value);
            }
            return add_node(out, {.kind = node_kind::byte, .byte = static_cast<std::uint8_t>(byte_value)});
          }
      }
    }

    /*!
     * \brief Parses one member inside a character class.
     * \param[in,out] klass The class being built; a set member (`\d` etc.) is
     *                   merged directly into it.
     * \return A single byte (usable as a range endpoint), or -1 when the member
     *         was a whole set merged into \p klass.
     * \throws real::regex_error on a non-ASCII member or an unsupported escape.
     */
    constexpr std::int32_t parse_class_item(char_class& klass)
    {
      const char ch {peek()};
      if (static_cast<std::uint8_t>(ch) >= 0x80) {
        // bytes mode keeps rejecting non-ASCII in a class (the compat layer relies on that rejection
        // to fall back to std). Code-point mode decodes the whole code point as a class member.
        if (bytes_) {
          fail("non-ASCII character class member not supported");
        }
        const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, pos_)};
        if (!decoded.valid) {
          fail("invalid UTF-8 byte in character class");
        }
        pos_ += decoded.length;
        return static_cast<std::int32_t>(decoded.cp); // a code point (may be >= 0x80)
      }
      if (ch != '\\') {
        ++pos_;
        return static_cast<std::uint8_t>(ch);
      }
      ++pos_; // consume the backslash
      if (eof()) {
        fail("dangling backslash");
      }
      switch (peek()) {
        case 'd':
          ++pos_;
          klass.merge(digit_set());
          return -1;
        case 'w':
          ++pos_;
          klass.merge(word_set());
          return -1;
        case 's':
          ++pos_;
          klass.merge(space_set());
          return -1;
        case 'D':
        case 'W':
        case 'S':
          fail("complemented set not supported inside a character class");
        case 'b':
          ++pos_;
          return 0x08; // backspace, only inside classes
        case 'u':
        case 'U':
          {
            const bool capital {peek() == 'U'};
            ++pos_;
            // A non-ASCII code point is now a valid class member (code-point mode); `parse_unicode_codepoint`
            // already rejects `\u`/`\U` in bytes mode, so a class in bytes mode still has ASCII-only members.
            return parse_unicode_codepoint(capital);
          }
        case 'N':
          fail("named Unicode escapes (\\N{...}) are not supported");
        default:
          {
            const std::int32_t byte_value {parse_byte_escape()};
            if (byte_value < 0) {
              fail("unsupported escape sequence");
            }
            return byte_value;
          }
      }
    }

    /*!
     * \brief Parses a bracketed character class `[...]` or `[^...]`.
     *
     * Supports ranges, escapes and the embedded set escapes; a `]` right after
     * `[` or `[^` is a literal, and a trailing `-` is a literal dash.
     *
     * \param[in,out] out The AST being built.
     * \return The index of the \ref node_kind::klass node.
     * \throws real::regex_error on an unterminated class or a bad range.
     */
    constexpr std::int32_t parse_class(ast& out)
    {
      const std::size_t open_pos      {pos_};
      ++pos_;                         // consume '['
      const bool              negated {accept('^')};
      char_class              klass;
      std::vector<code_range> ranges; // non-ASCII members (code-point mode); empty in bytes/ASCII-only classes
      bool                    first   {true};
      // Add one member. In bytes mode a member >= 0x80 (from `\xHH`) is a raw byte in the bitmap, NOT
      // a code point — so class_ranges stays empty and a bytes-mode class is byte-for-byte a
      // std::basic_regex<char> class (what the compat layer relies on). In code-point mode, >= 0x80 is
      // a (degenerate) code-point range.
      const auto add_cp {[&](std::int32_t cp) {
                           if (bytes_ || cp < 0x80) {
                             klass.set(static_cast<std::uint8_t>(cp));
                           }
                           else {
                             ranges.push_back({static_cast<std::uint32_t>(cp), static_cast<std::uint32_t>(cp)});
                           }
                         }};
      // Add an inclusive range [lo, hi]. Bytes mode: the whole range is bytes in the bitmap.
      // Code-point mode: a range crossing 0x7F/0x80 splits (the ASCII part -> bitmap).
      const auto add_range {[&](std::int32_t lo, std::int32_t hi) {
                              if (bytes_) {
                                klass.set_range(static_cast<std::uint8_t>(lo), static_cast<std::uint8_t>(hi));
                              }
                              else if (lo < 0x80) {
                                klass.set_range(static_cast<std::uint8_t>(lo), static_cast<std::uint8_t>(hi < 0x80 ? hi : 0x7F));
                                if (hi >= 0x80) {
                                  ranges.push_back({0x80U, static_cast<std::uint32_t>(hi)});
                                }
                              }
                              else {
                                ranges.push_back({static_cast<std::uint32_t>(lo), static_cast<std::uint32_t>(hi)});
                              }
                            }};
      while (true) {
        if (eof()) {
          pos_ = open_pos;
          fail("unterminated character class");
        }
        // Python (default): a ']' right after '[' or '[^' is a literal member, so `[]`/`[^]`
        // continue. ECMAScript (ecma): ']' always closes — `[]` is the empty class (matches
        // nothing) and `[^]` is its negation (matches any character, the "any incl. newline" idiom).
        if (peek() == ']' && (!first || ecma_)) {
          ++pos_;
          break;
        }
        first = false;
        const std::size_t  item_pos     {pos_};
        const std::int32_t range_start  {parse_class_item(klass)};
        if (range_start < 0) {
          continue; // set item: nothing more to do
        }
        // Possible range: 'x-y', where a trailing '-]' is a literal '-'.
        if (!eof() && peek() == '-' && pos_ + 1 < pattern_.size() &&
            pattern_[pos_ + 1] != ']') {
          ++pos_; // consume '-'
          const std::int32_t range_end {parse_class_item(klass)};
          if (range_end < 0 || range_end < range_start) {
            pos_ = item_pos;
            fail("bad character range");
          }
          add_range(range_start, range_end);
        }
        else {
          add_cp(range_start);
        }
      }
      return add_class_node(out, klass, negated, ranges);
    }
  };

  /*!
   * \brief Parses \p pattern into an \ref ast (convenience over \ref parser).
   * \param[in] pattern       The pattern text.
   * \param[in] initial_flags Constructor flags; only `verbose` affects parsing.
   * \return The parsed AST.
   * \throws real::regex_error on unsupported or malformed syntax.
   */
  constexpr ast parse(std::string_view pattern,
                      flags            initial_flags = flags::none)
  {
    return parser(pattern, initial_flags).parse();
  }
} // namespace real::detail

#endif // REAL_AST_HPP
