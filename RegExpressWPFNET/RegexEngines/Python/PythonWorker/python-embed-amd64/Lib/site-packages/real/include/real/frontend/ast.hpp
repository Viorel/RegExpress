/*!
 * \file ast.hpp
 * \brief Pattern text → AST, via a constexpr recursive-descent parser.
 *
 * Nodes live in an index-based pool (no pointers, so constexpr-friendly). Syntax the pipeline does
 * not implement is a \ref real::regex_error.
 *
 * Code-point mode (the default): a class carries non-ASCII members and ranges beside its ASCII bitmap
 * and compiles to the canonical UTF-8-ranges automaton (never an overlong or surrogate encoding);
 * every construct consumes whole code points, so a match boundary never splits a sequence. Bytes
 * mode: the unit is a byte, and a non-ASCII class member is the bytes it is written with, as in `re`
 * on a bytes pattern and `std::regex<char>`.
 */
#ifndef REAL_AST_HPP
#define REAL_AST_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "real/core/charclass.hpp"
#include "real/core/config.hpp"
#include "real/core/program.hpp"
#include "real/unicode/unicode_binprop.hpp"
#include "real/unicode/unicode_fold.hpp"
#include "real/unicode/unicode_property.hpp"
#include "real/unicode/unicode_props.hpp"
#include "real/unicode/unicode_script.hpp"
#include "real/unicode/unicode_scx.hpp"
#include "real/unicode/utf8.hpp"

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
    node_kind     kind            {node_kind::empty};   //!< Which fields below are meaningful.
    std::uint8_t  byte            {};                   //!< byte: the exact byte value.
    anchor_kind   anchor          {anchor_kind::caret}; //!< anchor: the assertion kind.
    bool          negated         {};                   //!< klass: written as `[^...]` / `\D` `\W` `\S`.
    bool          raw_byte        {};                   //!< any: `\C`, exactly one byte; accepted only under flags::bytes or flags::allow_raw_byte.
    bool          lazy            {};                   //!< repeat: prefer the shortest expansion.
    bool          possessive      {};                   //!< repeat: no give-back (`X*+`, `X{n,m}+`); group: atomic `(?>...)`.
    look_dir      direction       {look_dir::ahead};    //!< lookaround: ahead `(?=`/`(?!` or behind `(?<=`/`(?<!`.
    std::int32_t  klass           {-1};                 //!< klass: index into \ref ast::classes.
    std::int32_t  min             {};                   //!< repeat: minimum count.
    std::int32_t  max             {-1};                 //!< repeat: maximum count (-1 = unbounded).
    std::int32_t  group           {-1};                 //!< group: capture number, -1 for `(?:...)`.
    std::int32_t  child           {-1};                 //!< First child (concat, repeat, alternation, group).
    std::int32_t  next            {-1};                 //!< Next sibling in the parent's child list.
    std::uint16_t effective_flags {};                   //!< \ref flags in force where this node was parsed, stamped from the scope stack.
  };

  /*!
   * \brief A parsed character class: its ASCII bitmap plus its non-ASCII code-point ranges, bundled so
   *        the two cannot desynchronize.
   */
  struct class_def
  {
    char_class              ascii;                    //!< ASCII members as a bitmap (all 256 bytes in bytes mode); pre-negation.
    std::vector<code_range> ranges;                   //!< Non-ASCII code-point ranges (code-point mode only; empty otherwise).
    bool                    codepoint_predicate {};   //!< Emit as a match-time `klass_cp` (a Unicode shorthand `\w`/`\d`/`\s` in text mode), not the byte-NFA.
  };

  /*!
   * \brief Sorts \p ranges and merges overlapping or adjacent ones: the same code points in the fewest
   *        ranges.
   * \param[in] ranges The ranges to normalise; may be unsorted and overlapping.
   * \return The minimal sorted equivalent.
   */
  constexpr std::vector<code_range> coalesce_ranges(std::vector<code_range> ranges)
  {
    // Already sorted and disjoint (every shorthand / property table): return as is. Sorting the large
    // word table would exceed a static_regex's constexpr step budget.
    bool tidy {true};
    for (std::size_t i = 1; i < ranges.size(); ++i) {
      if (ranges[i].lo <= ranges[i - 1].hi + 1U) { // unsorted, overlapping, or adjacent
        tidy = false;
        break;
      }
    }
    if (tidy) {
      return ranges;
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const code_range& a, const code_range& b) { return a.lo < b.lo; });
    std::vector<code_range> merged;
    for (const code_range& r : ranges) {
      if (!merged.empty() && r.lo <= merged.back().hi + 1U) { // overlapping OR adjacent
        merged.back().hi = merged.back().hi > r.hi ? merged.back().hi : r.hi;
      }
      else {
        merged.push_back(r);
      }
    }
    return merged;
  }

  /*!
   * \brief Complements a set of code-point ranges within `[0x80, 0x10FFFF]` (negated classes, in-class
   *        `\W`/`\D`/`\S`). Input may be unsorted or overlapping; the gaps come sorted.
   * \param[in] ranges The ranges to complement.
   * \return The gaps between them within `[0x80, 0x10FFFF]`, sorted.
   */
  constexpr std::vector<code_range> complement_code_ranges(std::vector<code_range> ranges)
  {
    const std::vector<code_range> merged {coalesce_ranges(std::move(ranges))};
    std::vector<code_range>       gaps;
    std::uint32_t                 next   {0x80U};
    for (const code_range& r : merged) {
      if (r.lo > next) {
        gaps.push_back({.lo = next, .hi = r.lo - 1U});
      }
      next = r.hi + 1U;
    }
    if (next <= 0x10FFFFU) {
      gaps.push_back({.lo = next, .hi = 0x10FFFFU});
    }
    return gaps;
  }

  /*!
   * \brief A parsed pattern: the node pool plus side tables.
   *
   * Resource caps live in config.hpp (\ref max_repeat_count, \ref max_group_count,
   * \ref max_nesting_depth, \ref max_program_size).
   */
  struct ast
  {
    std::vector<ast_node>    nodes;                        //!< The node pool; \ref root indexes it.
    std::vector<class_def>   classes;                      //!< Character classes as written, before negation.
    std::vector<named_group> names;                        //!< Named capture groups.
    flags                    inline_flags   {flags::none}; //!< Flags a leading `(?imsxaU)` group ADDED (OR-only, hence the companion below).
    flags                    inline_removed {flags::none}; //!< Flags a leading `(?flags-flags)` group REMOVED; the caller clears these after OR-ing \ref inline_flags.
    std::int32_t             group_count    {};            //!< Number of capturing groups.
    std::int32_t             root           {-1};          //!< Index of the root node.
  };

  inline constexpr std::uint32_t not_a_single_codepoint {0xFFFFFFFFU}; //!< Returned by \ref single_codepoint_atom when the node is not one code point's bytes.

  /*!
   * \brief The code point a node spells, when it is exactly one non-ASCII literal character.
   *
   * A non-ASCII literal in text mode parses to a `concat` of its UTF-8 bytes (`é` is
   * `concat(byte C3, byte A9)`); the parser (possessive eligibility) and the compiler (unbounded
   * repeat as a code-point class) both ask here. The strict decode must consume every byte, so a
   * concat of more than one code point (`(?:éé)`, `(?:ab)`) is refused: as one atom it would change
   * what a quantifier repeats.
   *
   * \param[in] tree  The AST holding \p index.
   * \param[in] index The node to inspect.
   * \return The code point, or \ref not_a_single_codepoint.
   */
  [[nodiscard]] constexpr std::uint32_t single_codepoint_atom(const ast&   tree,
                                                              std::int32_t index)
  {
    if (index < 0) {
      return not_a_single_codepoint;
    }
    const ast_node& node {tree.nodes[static_cast<std::size_t>(index)]};
    if (node.kind != node_kind::concat) {
      return not_a_single_codepoint;
    }
    std::array<char, 4> seq {};
    std::size_t         len {0};
    for (std::int32_t walk = node.child; walk >= 0;) {
      const ast_node& w {tree.nodes[static_cast<std::size_t>(walk)]};
      if (w.kind != node_kind::byte || len == seq.size()) {
        return not_a_single_codepoint;
      }
      seq[len++] = static_cast<char>(w.byte);
      walk       = w.next;
    }
    if (len < 2) {
      return not_a_single_codepoint;
    }
    const decoded_codepoint dec {decode_codepoint_strict(std::string_view {seq.data(), len}, 0)};
    return (dec.valid && dec.length == len) ? dec.cp : not_a_single_codepoint;
  }

  /*!
   * \brief What a `\<digit>` escape decoded to (see decode_digit_escape()).
   */
  enum class digit_escape_kind : std::uint8_t
  {
    octal,          //!< An octal byte escape; `value` is the byte (0-255).
    group_ref,      //!< A decimal group number; `value` is the group (a back-reference in a pattern).
    octal_overflow, //!< A 3-octal-digit escape greater than 0o377 (an error in CPython).
  };

  /*! \brief Result of \ref decode_digit_escape. */
  struct digit_escape_result
  {
    digit_escape_kind kind   {digit_escape_kind::group_ref}; //!< Which interpretation applies.
    unsigned          value  {};                             //!< Octal byte, or decimal group number.
    std::size_t       length {};                             //!< Characters consumed from the first digit.
  };

  /*!
   * \brief Decodes a `\<digit>` escape per CPython's rule; shared by the pattern and the
   *        replacement-template parsers so the two never drift.
   *
   * A `\0` prefix, or a 1-7 digit followed by two more octal digits, is an octal escape (`\0`:
   * value & 0xff; the 3-digit form errors above 0o377). Otherwise the digits (at most two) are a
   * decimal group number.
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
        bytes_(has_flag(initial_flags, flags::bytes)),
        ecma_(has_flag(initial_flags, flags::ecma))
    {
      // Scope-stack base; a scoped group `(?flags:...)` pushes a modified copy for its body.
      flag_scopes_.push_back(initial_flags);
    }

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

    std::string_view   pattern_;          //!< The pattern being parsed.
    std::size_t        pos_           {}; //!< Current read offset into \ref pattern_.
    std::int32_t       depth_         {}; //!< Current group nesting (see \ref max_nesting_depth).
    std::vector<flags> flag_scopes_;      //!< Flag set in force per nesting level; the top is current (\ref current_flags). Read flags here, never from a member, so scoped groups are honoured.
    bool               in_lookaround_ {}; //!< True while parsing a lookaround sub-pattern (rejects nesting).
    bool               bytes_         {}; //!< In \ref flags::bytes mode, rejects code-point escapes (`\u`/`\U`).
    bool               ecma_          {}; //!< ECMAScript grammar: `\A \Z \< \>` are identity-escape literals, not anchors.

    /*!
     * \brief The flag set in force at the current nesting level (the scope-stack top).
     * \return The active flags.
     */
    [[nodiscard]] constexpr flags current_flags() const
    {
      return flag_scopes_.back();
    }

    /*!
     * \brief True when verbose mode (`re.X`) is in force at the current scope.
     * \return Whether \ref flags::verbose is active here.
     */
    [[nodiscard]] constexpr bool is_verbose() const
    {
      return has_flag(current_flags(), flags::verbose);
    }

    /*!
     * \brief True in the ECMAScript grammar. Not scopable, so every scope carries it; read it here,
     *        not from `ecma_`, which the flag-scope ratchet counts.
     * \return Whether \ref flags::ecma is active.
     */
    [[nodiscard]] constexpr bool is_ecma() const
    {
      return has_flag(current_flags(), flags::ecma);
    }

    /*!
     * \brief True when icase (`re.I`) is in force at the current scope.
     * \return Whether \ref flags::icase is active here.
     */
    [[nodiscard]] constexpr bool is_icase() const
    {
      return has_flag(current_flags(), flags::icase);
    }

    /*!
     * \brief True when ascii (`re.A`) is in force at the current scope.
     * \return Whether \ref flags::ascii is active here.
     */
    [[nodiscard]] constexpr bool is_ascii_mode() const
    {
      return has_flag(current_flags(), flags::ascii);
    }

    /*!
     * \brief In verbose mode, consumes insignificant whitespace and `#` comments.
     *
     * Called only between tokens outside classes; an escaped space (`\ `) is a literal read by the
     * escape parser.
     */
    constexpr void skip_insignificant()
    {
      if (!is_verbose()) {
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
     * A template so the always-throwing body stays a legal constexpr function (the IFNDR rule
     * spares templates); under constant evaluation the throw fails compilation with \p message
     * in the trace.
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
     * \brief Like \ref fail, but tags the error `unsupported` (well-formed but beyond the linear
     *        engine, e.g. a backreference or a nested lookaround) so a binding classifies it without
     *        reading the message. Templated for the same reason as \ref fail.
     * \param[in] message The diagnostic text, reported at the current read offset.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_unsupported(const char* message) const
    {
      throw regex_error(message, pos_, error_kind::unsupported);
    }

    /*!
     * \brief Fails an unrecognised `(?…` extension the way `re` does: naming it.
     *
     * As `re`, the message quotes everything consumed since the `?` plus the failing character
     * (`(?z)` names `?z`, `(?P)` names `?P)`) and the offset is the `?`'s. At end of pattern it is
     * `unexpected end of pattern` at the read offset.
     *
     * \param[in] question_pos Offset of the `?` in `(?` — `open_pos + 1` at every call site.
     * \throws real::regex_error always.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_unknown_extension(std::size_t question_pos) const
    {
      if (eof()) {
        fail("unexpected end of pattern");
      }
      // A backslash is quoted with the character it escapes: `(?\)` names `?\)`, not `?\`.
      std::size_t last {pos_};
      if (pattern_[last] == '\\' && last + 1 < pattern_.size()) {
        ++last;
      }
      // The message must stay valid UTF-8, or a binding decoding what() raises a decoding error
      // instead of this one. Text mode quotes a whole valid code point, as `re` does; an invalid
      // high byte, and every high byte in bytes mode, is written `\xHH`. Bytes below 0x80 stay raw.
      const bool bytes_mode  {has_flag(current_flags(), flags::bytes)};
      bool       escape_high {bytes_mode};
      if (!bytes_mode && static_cast<std::uint8_t>(pattern_[last]) >= 0x80U) {
        const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, last)};
        if (decoded.valid) {
          last += decoded.length - 1;
        }
        else {
          escape_high = true;
        }
      }
      constexpr std::string_view hex     {"0123456789abcdef"};
      std::string                message {"unknown extension ?"};
      for (std::size_t i = question_pos + 1; i <= last; ++i) {
        const std::uint8_t byte {static_cast<std::uint8_t>(pattern_[i])};
        if (escape_high && byte >= 0x80U) {
          message += "\\x";
          message += hex[byte >> 4U];
          message += hex[byte & 0x0FU];
        }
        else {
          message += pattern_[i];
        }
      }
      throw regex_error(message, question_pos);
    }

    /*!
     * \brief Returns `true` if the read offset is at or past the end of the pattern.
     * \return Whether the pattern is exhausted.
     */
    [[nodiscard]] constexpr bool eof() const
    {
      return pos_ >= pattern_.size();
    }

    /*!
     * \brief Returns the current character without consuming it (undefined at eof()).
     * \return The character at the read offset.
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
    constexpr std::int32_t add_node(ast&     out,
                                    ast_node node)
    {
      // The compiler reads these per node for scoped-flag semantics.
      node.effective_flags = static_cast<std::uint16_t>(current_flags());
      out.nodes.push_back(node);
      return static_cast<std::int32_t>(out.nodes.size()) - 1;
    }

    /*!
     * \brief Interns a class bitmap and appends a \ref node_kind::klass node.
     * \param[in,out] out     The AST being built.
     * \param[in]     klass   The class bitmap as written (before negation).
     * \param[in]     negated Whether the class was written negated.
     * \param[in]     ranges  Non-ASCII code-point ranges (code-point mode; empty otherwise).
     * \param[in]     codepoint_predicate Emit as a match-time `klass_cp`, not the byte-NFA.
     * \return The index of the new node.
     */
    constexpr std::int32_t add_class_node(ast&                           out,
                                          const char_class&              klass,
                                          bool                           negated,
                                          const std::vector<code_range>& ranges              = {},
                                          bool                           codepoint_predicate = false)
    {
      out.classes.push_back({.ascii = klass, .ranges = ranges, .codepoint_predicate = codepoint_predicate});
      const auto index {static_cast<std::int32_t>(out.classes.size()) - 1};
      return add_node(out, {.kind = node_kind::klass, .negated = negated, .klass = index});
    }

    /*!
     * \brief Whether a shorthand (`\d \w \s`) should be a text-mode Unicode code-point predicate:
     *        true in the default text mode, false in bytes mode or under `flags::ascii` (`re.A`).
     * \return Whether the shorthand compiles as a code-point predicate rather than a byte class.
     */
    [[nodiscard]] constexpr bool text_shorthand() const
    {
      return !bytes_ && !is_ascii_mode();
    }

    /*!
     * \brief Merges an in-class shorthand (`\w \d \s`, or negated `\W \D \S`) into the class being
     *        built: its ASCII bitmap plus, in text mode, its non-ASCII ranges (each complemented when
     *        negated). In text mode, sets \p property_derived.
     *
     * \param[in,out] klass            The class being built, receiving the ASCII bitmap.
     * \param[in,out] ranges           The class's non-ASCII ranges, appended to in text mode.
     * \param[in]     prop_ascii       The shorthand's ASCII bitmap.
     * \param[in]     table            The shorthand's full Unicode range table.
     * \param[in]     negated          True for the uppercase form (`\W \D \S`).
     * \param[out]    property_derived Set when the class must be emitted as a `klass_cp`.
     */
    constexpr void merge_property(char_class&                 klass,
                                  std::vector<code_range>&    ranges,
                                  const char_class&           prop_ascii,
                                  std::span<const code_range> table,
                                  bool                        negated,
                                  bool&                       property_derived) const
    {
      if (negated) {
        char_class inverted {prop_ascii};
        if (bytes_) {
          inverted.invert(); // raw bytes: plain 256-bit complement, no ranges
          klass.merge(inverted);
          return;
        }
        inverted.invert_ascii();
        klass.merge(inverted);
        // Under re.A shorthand_ranges is empty, so the complement is all non-ASCII (`\W` matches é).
        const std::vector<code_range> comp {complement_code_ranges(shorthand_ranges(table))};
        ranges.insert(ranges.end(), comp.begin(), comp.end());
      }
      else {
        klass.merge(prop_ascii);
        const std::vector<code_range> high {shorthand_ranges(table)};
        ranges.insert(ranges.end(), high.begin(), high.end());
      }
      property_derived = property_derived || text_shorthand();
    }

    /*!
     * \brief The non-ASCII part of a shorthand's range table, or nothing in bytes / ASCII mode.
     *        Wholly-ASCII ranges are dropped: the bitmap already covers them.
     * \param[in] table The shorthand's full Unicode range table.
     * \return Its ranges clipped to `>= 0x80`; empty when the shorthand stays ASCII-only.
     */
    [[nodiscard]] constexpr std::vector<code_range> shorthand_ranges(std::span<const code_range> table) const
    {
      std::vector<code_range> out;
      if (bytes_ || is_ascii_mode()) {
        return out;
      }
      for (const code_range& r : table) {
        if (r.hi < 0x80U) {
          continue; // wholly ASCII: already covered by the bitmap
        }
        out.push_back({.lo = r.lo < 0x80U ? 0x80U : r.lo, .hi = r.hi});
      }
      return out;
    }

    /*!
     * \brief The classification of a `\d \D \w \W \s \S` shorthand: its ASCII bitmap, its Unicode range
     *        table, and whether it is the negated (uppercase) form.
     */
    struct shorthand_spec
    {
      char_class                  set;     //!< The ASCII bitmap (digit / word / space set).
      std::span<const code_range> ranges;  //!< The full Unicode range table (used in text mode).
      bool                        negated; //!< True for the uppercase form (`\D \W \S`).
    };

    /*!
     * \brief `\s`'s ASCII bitmap in TEXT mode: `space_set()` (`[ \t\n\r\f\v]`, the ASCII-mode set) plus
     *        `U+001C`-`U+001F`. `re` matches FS/GS/RS/US with `\s` but not with `(?a)\s`, and
     *        `shorthand_ranges` drops wholly-ASCII ranges, so the bitmap itself must differ by mode.
     * \return The text-mode ASCII bitmap for `\s`.
     */
    [[nodiscard]] static constexpr char_class space_set_text_ascii_component()
    {
      char_class result {space_set()};
      result.set(char {0x1C}); // FS
      result.set(char {0x1D}); // GS
      result.set(char {0x1E}); // RS
      result.set(char {0x1F}); // US
      return result;
    }

    /*!
     * \brief Maps a shorthand letter to its \ref shorthand_spec; the one place this fact lives, shared
     *        by the atom (parse_escape) and in-class (parse_class_item) paths.
     * \param[in] letter    The shorthand letter (`d D w W s S`).
     * \param[in] text_mode The caller's \ref text_shorthand; only `\s`/`\S` read it (see
     *                      \ref space_set_text_ascii_component).
     * \return The shorthand's bitmap, range table and negation flag.
     */
    [[nodiscard]] static constexpr shorthand_spec shorthand_class(char letter,
                                                                  bool text_mode)
    {
      switch (letter) {
        case 'd': return {.set = digit_set(), .ranges = digit_ranges, .negated = false};
        case 'D': return {.set = digit_set(), .ranges = digit_ranges, .negated = true};
        case 'w': return {.set = word_set(), .ranges = word_ranges, .negated = false};
        case 'W': return {.set = word_set(), .ranges = word_ranges, .negated = true};
        case 's':
          return {.set = text_mode ? space_set_text_ascii_component() : space_set(),
                  .ranges = space_ranges, .negated = false};
        case 'S':
          return {.set = text_mode ? space_set_text_ascii_component() : space_set(),
                  .ranges = space_ranges, .negated = true};
        default: break;
      }
      // Unreachable: both callers dispatch only on the six shorthand letters.
      return {.set = space_set(), .ranges = space_ranges, .negated = true};
    }

    /*!
     * \brief A loose-match key (lowercase, no `_`/`-`/space) in a fixed buffer, so parsing needs no heap
     *        or `<string>`. A name longer than the buffer matches nothing.
     */
    struct loose_buf
    {
      std::array<char, 64> data {}; //!< The normalised bytes, the first \ref len of which are meaningful.
      std::size_t          len  {}; //!< Bytes held in \ref data.

      /*!
       * \brief The normalised key as a view into \ref data.
       * \return A view valid for this buffer's lifetime.
       */
      [[nodiscard]] constexpr std::string_view view() const
      {
        return {data.data(), len};
      }
    };

    /*!
     * \brief Loose-matches a property name (UAX44-LM3): drops `_`, `-` and spaces, lowercases the rest.
     * \param[in] s The name as written in the pattern.
     * \return The normalised key, truncated to the buffer's capacity.
     */
    [[nodiscard]] static constexpr loose_buf loose_key(std::string_view s)
    {
      loose_buf b;
      for (const char c : s) {
        if (c == '_' || c == '-' || c == ' ') {
          continue;
        }
        if (b.len < b.data.size()) {
          b.data[b.len++] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
      }
      return b;
    }

    /*!
     * \brief Resolves a `\p{...}` property name to its code-point ranges, or fails.
     *
     * A `gc=` / `sc=` / `scx=` prefix (or its long form) picks the namespace. As in PCRE2, a bare name
     * tries General_Category, then Script, then a binary property, and never means Script_Extensions.
     * A Script's ranges are gathered from the script partition; GC, binary-property and scx ranges come
     * from their own tables (the last two are not partitions).
     *
     * \param[in] name The property name as written, prefix and all.
     * \return Its code-point ranges.
     */
    [[nodiscard]] constexpr std::vector<code_range> resolve_property(std::string_view name) const
    {
      std::string_view ns;
      std::string_view value {name};
      for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '=') {
          ns    = name.substr(0, i);
          value = name.substr(i + 1);
          break;
        }
      }
      const loose_buf        ns_key      {loose_key(ns)};
      const std::string_view nk          {ns_key.view()};
      const bool             want_gc     {nk.empty() || nk == "gc" || nk == "generalcategory"};
      const bool             want_script {nk.empty() || nk == "sc" || nk == "script"};
      const bool             want_scx    {nk == "scx" || nk == "scriptextensions"};
      if (!want_gc && !want_script && !want_scx) {
        // Well-formed but not offered: unsupported, so a binding may delegate it.
        fail_unsupported("unknown Unicode property namespace in \\p{...} (use gc=, sc= or scx=)");
      }
      const loose_buf value_key {loose_key(value)};
      if (want_gc) {
        const gc_property prop {resolve_gc(value_key.view())};
        if (prop != gc_property::count) {
          const std::span<const code_range> t {gc_property_ranges[static_cast<std::size_t>(prop)]};
          return {t.begin(), t.end()};
        }
      }
      if (want_script) {
        const script sc {resolve_script(value_key.view())};
        if (sc != script::count) {
          std::vector<code_range> out;
          for (const script_range& r : script_ranges) {
            if (r.sc == sc) {
              out.push_back({.lo = r.lo, .hi = r.hi});
            }
          }
          return out;
        }
      }
      if (want_scx) {
        const script sc {resolve_script(value_key.view())};
        if (sc != script::count) {
          const std::span<const code_range> t {scx_ranges[static_cast<std::size_t>(sc)]};
          return {t.begin(), t.end()};
        }
      }
      if (nk.empty()) {
        // Only a bare name tries a binary property: a name that failed in an explicit namespace fails.
        const binprop bp {resolve_binprop(value_key.view())};
        if (bp != binprop::count) {
          const std::span<const code_range> t {binprop_ranges[static_cast<std::size_t>(bp)]};
          return {t.begin(), t.end()};
        }
        // `\p{Any}` (every code point, as in RE2/Perl/ECMAScript) has no UCD table; bare name only.
        if (value_key.view() == "any") {
          return {{.lo = 0x0, .hi = 0x10FFFF}};
        }
      }
      // An unknown name is unsupported rather than an error, so a binding can delegate it.
      fail_unsupported("unsupported Unicode property in \\p{...} (General_Category, Script, "
                       "Script_Extensions and the standard binary properties are built in)");
    }

    /*!
     * \brief The result of \ref parse_property_table — the ranges, and whether a leading caret was stripped
     *        (`\p{^L}` == `\P{L}`, RE2/Perl). Callers XOR `caret` into their own negation so it composes
     *        with `\P` and `[^...]`.
     */
    struct property_table_result
    {
      std::vector<code_range>  ranges; //!< The property's ranges, never caret-adjusted.
      bool                     caret;  //!< True when a leading `^` (native dialects only) was stripped.
    };

    /*!
     * \brief Consumes `p`/`P` and `{Name}` (or one letter), strips a leading `^` (native dialects only)
     *        and resolves the name; rejects bytes mode. Shared by the atom and the in-class paths.
     *        Entered on the `p`/`P`; leaves `pos_` just past the name.
     * \return The resolved ranges and whether a caret-negation was stripped.
     */
    constexpr property_table_result parse_property_table()
    {
      // From the scope stack, not `bytes_` (counted by the flag-scope ratchet); bytes is never scoped.
      if (has_flag(current_flags(), flags::bytes)) {
        fail_unsupported("\\p{...} Unicode property classes are not available in bytes mode");
      }
      ++pos_; // consume the 'p' / 'P'
      std::string_view name;
      if (!eof() && peek() == '{') {
        ++pos_;
        const std::size_t start {pos_};
        while (!eof() && peek() != '}') {
          ++pos_;
        }
        if (eof()) {
          fail("unterminated \\p{...} property");
        }
        name = pattern_.substr(start, pos_ - start);
        ++pos_; // consume '}'
      }
      else {
        if (eof()) {
          fail("\\p must be followed by a property name or {name}");
        }
        name = pattern_.substr(pos_, 1);
        ++pos_;
      }
      if (name.empty()) {
        fail("empty Unicode property name in \\p{}");
      }
      // Caret-negation is RE2/Perl, a SyntaxError in ECMAScript: under ecma the `^` stays in `name` and
      // resolve_property rejects it. It precedes any namespace (`\p{^gc=L}`).
      bool caret {false};
      if (!is_ecma() && !name.empty() && name.front() == '^') {
        caret = true;
        name.remove_prefix(1);
      }
      return {.ranges = resolve_property(name), .caret = caret};
    }

    /*!
     * \brief Splits a property's ranges into its ASCII bitmap (< 0x80) and its non-ASCII ranges. Unlike
     *        `\w`, a Unicode property is not restricted by `flags::ascii`.
     * \param[in]  table The property's full range table.
     * \param[out] ascii Bitmap receiving its members below 0x80.
     * \param[out] high  Ranges receiving its members at or above 0x80.
     */
    constexpr void property_ascii_high(const std::vector<code_range>& table,
                                       char_class&                    ascii,
                                       std::vector<code_range>&       high) const
    {
      for (char32_t c = 0; c < 0x80U; ++c) {
        if (cp_in_ranges(table, c)) {
          ascii.set(static_cast<std::uint8_t>(c));
        }
      }
      for (const code_range& r : table) {
        if (r.hi < 0x80U) {
          continue; // wholly ASCII: already in the bitmap
        }
        high.push_back({.lo = r.lo < 0x80U ? 0x80U : r.lo, .hi = r.hi});
      }
    }

    /*!
     * \brief Parses `\p{Name}` / `\P{Name}` / `\pX` outside a class into a code-point class node
     *        (`klass_cp`, as `\w`). Negation is the node flag XORed with the caret (`\P{^L}` == `\p{L}`).
     *        Entered on the letter after `\`.
     *
     * \param[in,out] out     The AST the class node is added to.
     * \param[in]     negated True for `\P`, false for `\p`.
     * \return The new node's index.
     */
    constexpr std::int32_t parse_unicode_property(ast& out,
                                                  bool negated)
    {
      const property_table_result table {parse_property_table()};
      negated = negated != table.caret; // bool XOR without the int promotion misra rejects
      char_class                    ascii;
      std::vector<code_range>       high;
      property_ascii_high(table.ranges, ascii, high);
      return add_class_node(out, ascii, negated, high, /*codepoint_predicate=*/ true);
    }

    /*!
     * \brief Merges `\p{Name}` / `\P{Name}` into the class being built: \ref merge_property without the
     *        `flags::ascii` restriction. `\P` merges the complement, as `\W` does; an enclosing `[^...]`
     *        negates on top (`[^\P{L}]` == `[\p{L}]`).
     *
     * \param[in,out] klass            The class being built, receiving the ASCII bitmap.
     * \param[in,out] ranges           The class's non-ASCII ranges, appended to.
     * \param[in]     table            The property's full range table.
     * \param[in]     negated          True for `\P{...}`, merging the complement.
     * \param[out]    property_derived Set so the class is emitted as a `klass_cp`.
     */
    constexpr void merge_unicode_property(char_class&                    klass,
                                          std::vector<code_range>&       ranges,
                                          const std::vector<code_range>& table,
                                          bool                           negated,
                                          bool&                          property_derived) const
    {
      char_class              ascii;
      std::vector<code_range> high;
      property_ascii_high(table, ascii, high);
      if (negated) {
        ascii.invert_ascii();
        klass.merge(ascii);
        const std::vector<code_range> comp {complement_code_ranges(high)};
        ranges.insert(ranges.end(), comp.begin(), comp.end());
      }
      else {
        klass.merge(ascii);
        ranges.insert(ranges.end(), high.begin(), high.end());
      }
      property_derived = true;
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
     *
     * Also handles `\Q...\E` (RE2/Perl, not ecma) here, since it emits a sequence, not one atom. As in
     * RE2, a following quantifier binds to the span's last character (`\Qab\E+` == `ab+`), and an empty
     * `\Q\E` is invisible (`a\Q\E+` == `a+`): the quantifier re-binds to the previous atom (re-chained
     * through `prev`), or fails "nothing to repeat" when there is none.
     *
     * \param[in,out] out The AST being built.
     * \return The index of a concat node, a single atom, or an empty node.
     */
    constexpr std::int32_t parse_sequence(ast& out)
    {
      std::int32_t first {-1};
      std::int32_t last  {-1};
      std::int32_t prev  {-1}; // predecessor of `last`: the re-chain point after an empty \Q\E
      while (true) {
        skip_insignificant(); // verbose: between elements and before '|' / ')'
        if (eof() || peek() == '|' || peek() == ')') {
          break;
        }
        std::int32_t atom {-1};
        if (peek() == '\\' && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] == 'Q' && !is_ecma()) {
          pos_ += 2; // consume \Q
          const quoted_span span {parse_quoted_span(out)};
          if (span.last == -1) {
            if (last != -1) {
              skip_insignificant();
              const std::int32_t requant {parse_quantifier(out, last)};
              if (requant != last) {
                if (prev == -1) {
                  first = requant;
                }
                else {
                  out.nodes[static_cast<std::size_t>(prev)].next = requant;
                }
                last = requant;
              }
            }
            continue;
          }
          if (span.head != -1) { // chain the bare all-but-last prefix
            if (first == -1) {
              first = span.head;
            }
            else {
              out.nodes[static_cast<std::size_t>(last)].next = span.head;
            }
            last = span.head_tail;
          }
          atom = span.last; // the span's final atom takes the normal quantifier path below
        }
        else {
          atom = parse_atom(out);
        }
        skip_insignificant(); // verbose: whitespace between an atom and its quantifier
        atom = parse_quantifier(out, atom);
        if (first == -1) {
          first = atom;
        }
        else {
          out.nodes[static_cast<std::size_t>(last)].next = atom;
          prev                                           = last;
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
     * \brief What \ref parse_quoted_span emitted: the chained all-but-last prefix plus the final atom,
     *        the caller's quantifier target.
     */
    struct quoted_span
    {
      std::int32_t head      {-1}; //!< First atom of the all-but-last prefix chain (-1: none).
      std::int32_t head_tail {-1}; //!< Last atom of the prefix chain (-1: none).
      std::int32_t last      {-1}; //!< The span's final atom (-1: empty span).
    };

    /*!
     * \brief Scans a `\Q...\E` span (`\Q` already consumed) and emits its characters as literal atoms
     *        via \ref emit_literal_codepoint, as RE2 does.
     *
     * The span ends at `\E` (consumed) or at end of pattern. Everything inside is literal, including
     * whitespace in verbose mode and a backslash not followed by `E` (`\Qa\Qb\E` is `a\Qb`): no escape
     * processing, no nesting. All atoms but the last are chained; the last is left for the caller's
     * quantifier.
     *
     * \param[in,out] out The AST being built.
     * \return A \ref quoted_span (all members -1 for an empty `\Q\E`).
     * \throws real::regex_error on an invalid UTF-8 byte inside the span (text mode).
     */
    constexpr quoted_span parse_quoted_span(ast& out)
    {
      quoted_span  r;
      std::int32_t pending {-1}; // the previously scanned character, not yet known to be the last
      while (!eof()) {
        if (peek() == '\\' && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] == 'E') {
          pos_ += 2; // consume the \E terminator
          break;
        }
        std::int32_t cp {};
        // Bytes mode from the scope stack, not `bytes_` (see parse_property_table).
        if (!has_flag(current_flags(), flags::bytes) && static_cast<std::uint8_t>(peek()) >= 0x80U) {
          const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, pos_)};
          if (!decoded.valid) {
            fail("invalid UTF-8 byte in pattern");
          }
          pos_ += decoded.length;
          cp    = static_cast<std::int32_t>(decoded.cp);
        }
        else {
          cp = static_cast<std::uint8_t>(peek());
          ++pos_;
        }
        if (pending >= 0) { // the previous character is now known not-last: chain it bare
          const std::int32_t node {emit_literal_codepoint(out, pending)};
          if (r.head == -1) {
            r.head = node;
          }
          else {
            out.nodes[static_cast<std::size_t>(r.head_tail)].next = node;
          }
          r.head_tail = node;
        }
        pending = cp;
      }
      if (pending >= 0) {
        r.last = emit_literal_codepoint(out, pending);
      }
      return r;
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
            // Code-point mode: a non-ASCII byte opens a UTF-8 sequence, emitted whole as one atom (as
            // `\uHHHH`) so a quantifier repeats the code point, not its last byte; malformed is an
            // error. Bytes mode and ASCII: one byte node.
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
     * Grammar: `quantifier := ('*' | '+' | '?' | '{n}' | '{n,}' | '{,m}' | '{,}' | '{n,m}') '?'?`
     * (`{,}` is `{0,}`). As in Python, an ill-formed `{...}` stays literal (`a{`, `a{}`, `a{2,3x`). A
     * brace quantifier with no atom before it (`{2}a`, `{,3}a`) is literal here but `nothing to repeat`
     * in `re`: a deliberate divergence (div_compiles). A bare anchor cannot be repeated.
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
        const std::size_t quantifier_pos {pos_}; // captured before try_parse_braces CONSUMES
        std::int32_t      ignored_min    {};
        std::int32_t      ignored_max    {-1};
        if (peek() != '{' || try_parse_braces(ignored_min, ignored_max)) {
          // Report at the quantifier: a braced one has already consumed its body.
          pos_ = quantifier_pos;
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
      // (?U) (RE2) swaps the default: a bare quantifier is lazy, an explicit '?' greedy. Read from the
      // scope stack so `(?U:...)` scopes; resolved here into node.lazy.
      const bool explicit_q {accept('?')};
      const bool lazy       {has_flag(current_flags(), flags::ungreedy) ? !explicit_q : explicit_q};
      // Possessive is native-dialect only and excludes lazy: under ecma (as V8 and std) and under
      // (?U) (as RE2), the '+' stays and fails "multiple repeat" below.
      const bool possessive {!is_ecma() && !lazy && accept('+')};
      if (!eof()) {
        const std::size_t second_pos  {pos_}; // captured before try_parse_braces CONSUMES the braces
        const char        ch          {peek()};
        std::int32_t      ignored_min {};
        std::int32_t      ignored_max {-1};
        if (ch == '*' || ch == '+' || ch == '?' ||
            (ch == '{' && try_parse_braces(ignored_min, ignored_max))) {
          // Report at the second quantifier, not past a consumed `{n}`.
          pos_ = second_pos;
          fail("multiple repeat");
        }
      }
      // A possessive body must be one node kind, and a non-ASCII literal is a byte concat: promote it
      // to its one-member code-point class (`é++` as `[é]++`). Possessive only: a bounded `é{2}` stays
      // bytes so a literal run routes as an exact literal; unbounded is emit_unbounded_body's.
      std::int32_t body {atom};
      if (possessive) {
        const std::uint32_t cp {single_codepoint_atom(out, atom)};
        if (cp != not_a_single_codepoint) {
          const std::vector<code_range> single {{.lo = cp, .hi = cp}};
          body = add_class_node(out, char_class {}, false, single);
        }
      }
      return add_node(out, {.kind       = node_kind::repeat,
                            .lazy       = lazy,
                            .possessive = possessive,
                            .min        = min,
                            .max        = max,
                            .child      = body});
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
      // Both bounds absent is literal only without a comma: `{}` is text, `{,}` is `{0,}` (as `re`).
      if (!accept('}') || (repeat_min < 0 && repeat_max < 0 && !has_comma)) {
        pos_ = saved_pos;
        return false; // "{", "{}", "{x"…: literal text
      }
      min = repeat_min < 0 ? 0 : repeat_min;
      max = (has_comma && repeat_max < 0) ? -1 : repeat_max;
      if (max != -1 && max < min) {
        // Reported inside the braces, as `re` does (`saved_pos` is the `{`).
        pos_ = saved_pos + 1;
        fail("min repeat greater than max repeat");
      }
      return true;
    }

    /*!
     * \brief Reads an optional decimal repeat count.
     * \return The count, or -1 when no digits are present.
     * \throws real::regex_error if the count exceeds \ref max_repeat_count (counts are unrolled).
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
     * \param[in] letter One of 'i', 'm', 's', 'x', 'a', 'U'.
     * \return The flag; \ref flags::none for any unrecognized letter.
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
        case 'a': // ASCII mode: `\d \w \s \b` stay ASCII, icase folds ASCII only.
          return flags::ascii;
        case 'U': // Ungreedy mode (RE2 (?U)): swap the default quantifier greediness.
          return flags::ungreedy;
        default:
          return flags::none;
      }
    }

    /*!
     * \brief Returns `true` if \p letter is a flag letter (imsaxU).
     * \param[in] letter A character.
     * \return `true` if \p letter is a flag letter (imsaxU).
     */
    static constexpr bool is_flag_letter(char letter)
    {
      return letter == 'i' || letter == 'm' || letter == 's' || letter == 'a' || letter == 'x' ||
             letter == 'U';
    }

    /*!
     * \brief Returns `true` if the unit at the cursor is a LETTER — an unknown flag, not a terminator.
     *
     * CPython's `_parse_flags` splits on `str.isalpha()` over the mode's unit: the whole code point in
     * text mode, the byte read as latin-1 under `flags::bytes`. `gc_property::L` agrees with
     * `str.isalpha()` on every code point, so `(?ié` is `unknown flag` and `(?i😀)` a terminator fault.
     *
     * \return `true` if the cursor is on a letter, in the mode's own unit.
     */
    [[nodiscard]] constexpr bool at_letter_unit() const
    {
      if (eof()) {
        return false;
      }
      const std::uint8_t byte {static_cast<std::uint8_t>(peek())};
      char32_t           cp   {byte};
      if (byte >= 0x80U && !has_flag(current_flags(), flags::bytes)) {
        const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, pos_)};
        if (!decoded.valid) {
          return false; // not a code point, so not a letter; another check names that
        }
        cp = static_cast<char32_t>(decoded.cp);
      }
      return cp_in_ranges(gc_property_ranges[static_cast<std::size_t>(gc_property::L)], cp);
    }

    /*!
     * \brief Fails a `(?…` extension, naming it when it is one REAL excludes by design.
     *
     * Callouts, recursion and subroutine calls are non-regular (super-linear), so they are named and
     * tagged `unsupported` (\ref error_kind), reported at the character after `(?`. Anything else is
     * \ref fail_unknown_extension's, at the `?` as in `re`.
     * \param[in] question_pos Offset of the `?` in `(?`, forwarded to \ref fail_unknown_extension.
     * \throws real::regex_error always.
     */
    [[noreturn]] constexpr void fail_extension(std::size_t question_pos) const
    {
      if (!eof()) {
        const char lead {peek()};
        // (?C) / (?C1) / (?C"str") — PCRE callouts.
        if (lead == 'C') {
          fail_unsupported("callouts are not supported");
        }
        // (?R) whole-pattern, (?0) / (?3) absolute, (?+1) / (?-1) relative recursion.
        if (lead == 'R' || is_ascii_digit(lead)
            || ((lead == '+' || lead == '-') && pos_ + 1 < pattern_.size()
                && is_ascii_digit(pattern_[pos_ + 1]))) {
          fail_unsupported("pattern recursion is not supported");
        }
        // (?&name) — PCRE named subroutine call. (?P>name) is caught at the (?P branch.
        if (lead == '&') {
          fail_unsupported("subroutine calls are not supported");
        }
      }
      fail_unknown_extension(question_pos);
    }

    /*!
     * \brief Returns `true` if \p ch is an ASCII digit (`0`–`9`).
     * \param[in] ch A character.
     * \return `true` if \p ch is an ASCII digit.
     */
    static constexpr bool is_ascii_digit(char ch)
    {
      return ch >= '0' && ch <= '9';
    }

    /*!
     * \brief Consumes a run of inline flag letters (`imsxaU`).
     * \return The OR of the consumed flags (`flags::none` if the run was empty).
     */
    constexpr flags consume_flag_letters()
    {
      flags found {flags::none};
      while (!eof() && is_flag_letter(peek())) {
        found = found | flag_for_letter(peek());
        ++pos_;
      }
      return found;
    }

    /*!
     * \brief Fails with "unknown flag" if the next unit is a letter that is not a flag.
     *
     * As in CPython, this precedes the terminator check (\ref require_scoped_flags_colon). Call only
     * once a flags group has started (a flag letter or `-`): the global prefix must backtrack on
     * `(?P` / `(?#`, not call `P` an unknown flag.
     */
    constexpr void fail_if_unknown_flag()
    {
      if (at_letter_unit()) {
        fail("unknown flag");
      }
    }

    /*!
     * \brief After a flags run, requires `:` (scoped body) or fails naming the terminator fault.
     *
     * As `re`'s `_parse_flags`:
     *
     * - `:` — scoped body, consumed, return
     * - `)` — an unscoped group not at the start (`a(?i)b`; a leading one was taken by
     *   \ref parse_global_flags_prefix): `global flags not at the start of the expression`
     * - after a `-flags` run, anything else (including EOF) — `missing :`
     * - otherwise — `missing -, : or )` (`(?i*)`, `(?i7)`, `(?i`)
     *
     * \param[in] after_removal Whether the run just consumed a `-flags` suffix.
     * \param[in] open_pos      Offset of the group's `(`, where `re` reports the placement fault.
     */
    constexpr void require_scoped_flags_colon(bool        after_removal,
                                              std::size_t open_pos)
    {
      if (accept(':')) {
        return;
      }
      if (!eof() && peek() == ')') {
        pos_ = open_pos;
        fail("global flags not at the start of the expression");
      }
      if (after_removal) {
        fail("missing :");
      }
      fail("missing -, : or )");
    }

    /*!
     * \brief \p value with \p bit cleared, via \ref real::flags_without (a cast narrower than the 16-bit
     *        underlying type would drop `flags::ungreedy` from every scope).
     * \param[in] value The flag set to clear from.
     * \param[in] bit   The flag to clear.
     * \return \p value without \p bit.
     */
    static constexpr flags without(flags value,
                                   flags bit)
    {
      return flags_without(value, bit);
    }

    /*!
     * \brief Consumes a leading global-flags group -- `(?imsxaU)`, or `(?flags-flags)` with a removal
     *        suffix -- if present. The accepted letters are `i m s x a U` (\ref is_flag_letter).
     *
     * As in Python 3.11+, global flags are legal only at the very start (\ref parse_group rejects
     * later ones). As in RE2, a `-removed` suffix (`(?i-s)`, `(?-s)`) clears flags from the base
     * scope.
     *
     * \param[in,out] out Receives added letters in \ref ast::inline_flags and removed ones in
     *                    \ref ast::inline_removed — two fields, since inline_flags is OR-ed across
     *                    calls; the caller adds, then clears.
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
      const flags found      {consume_flag_letters()};
      const bool  any_letter {found != flags::none};
      if (any_letter) {
        fail_if_unknown_flag();
      }
      flags removed     {flags::none};
      bool  any_removed {};
      if (accept('-')) {
        removed = consume_flag_letters();
        fail_if_unknown_flag();
        if (removed == flags::none) {
          // Hard failure, not a backtrack: here `-` can only open a flags group.
          fail("missing flag");
        }
        any_removed = true;
      }
      if ((!any_letter && !any_removed) || !accept(')')) {
        pos_ = saved_pos; // some other (?...) construct, or a scoped (?flags-flags:...): let parse_group decide
        return false;
      }
      out.inline_flags   = out.inline_flags | found;
      out.inline_removed = out.inline_removed | removed;
      // Sets the base scope, as the constructor flags do, for the rest of the pattern.
      flag_scopes_.back() = without(current_flags() | found, removed);
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
     * Also lookarounds, atomic groups, comments and scoped flags. Unsupported extensions
     * (backreferences, conditionals, recursion, callouts) fail naming the feature. Under
     * `flags::ecma`, `(?#...)`, `(?P...` and `(?>...)` are "unknown extension". Nesting beyond
     * \ref max_nesting_depth is rejected.
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
      ++pos_;                            // consume '('
      std::int32_t group        {-1};
      bool         scoped_flags {false}; //!< A (?flags:...) group pushed a scope to pop after the body.
      if (accept('?')) {
        if (!is_ecma() && accept('#')) {
          // (?#...) ends at the first ')' (a backslash is not special, as in re); emits nothing.
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
        else if (!is_ecma() && accept('P')) {
          if (accept('<')) {
            group = new_group(out, open_pos);
            parse_group_name(out, group);
          }
          else if (!eof() && peek() == '=') {
            fail_unsupported("named backreferences are not supported");
          }
          else if (!eof() && peek() == '>') {
            fail_unsupported("subroutine calls are not supported");
          }
          else {
            fail_unknown_extension(open_pos + 1);
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
        else if (!is_ecma() && !eof() && peek() == '>') {
          return parse_atomic_group(out, open_pos);
        }
        else if (!eof() && peek() == '(') {
          fail_unsupported("conditional groups are not supported");
        }
        // `-` opens `(?-i:…)` unless a digit follows: `(?-1)` is recursion, for fail_extension.
        else if (!eof()
                 && (is_flag_letter(peek())
                     || (peek() == '-'
                         && (pos_ + 1 >= pattern_.size() || !is_ascii_digit(pattern_[pos_ + 1]))))) {
          // (?flags:...) / (?-flags:...) / (?flags-flags:...).
          const flags added   {consume_flag_letters()};
          fail_if_unknown_flag();
          flags removed       {flags::none};
          bool  after_removal {};
          if (accept('-')) {
            removed = consume_flag_letters();
            fail_if_unknown_flag();
            if (removed == flags::none) {
              fail("missing flag");
            }
            after_removal = true;
          }
          require_scoped_flags_colon(after_removal, open_pos);
          // Every inline flag (i m s x a U) is honoured per scope.
          flag_scopes_.push_back(without(current_flags() | added, removed));
          scoped_flags = true; // group stays non-capturing (-1)
        }
        else {
          fail_extension(open_pos + 1);
        }
      }
      else {
        group = new_group(out, open_pos);
      }
      const std::int32_t body {parse_alternation(out)};
      if (scoped_flags) {
        flag_scopes_.pop_back();
      }
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
     * Its capture groups take numbers (so outer numbering holds) but are compiled capture-free. A
     * nested lookaround is rejected; boundedness is the compiler's.
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
        fail_unsupported("nested lookaround is not supported");
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
     * \brief Parses an atomic group after `(?>` (the `>` is not yet consumed).
     *
     * A non-capturing \ref node_kind::group with `possessive = true`; a capture inside keeps its number.
     * Linearity restrictions are the compiler's.
     *
     * \param[in,out] out      The AST being built.
     * \param[in]     open_pos Offset of the group's `(` (for error reporting).
     * \return The index of the atomic group's \ref node_kind::group node.
     */
    constexpr std::int32_t parse_atomic_group(ast&        out,
                                              std::size_t open_pos)
    {
      ++pos_; // consume '>'
      const std::int32_t body {parse_alternation(out)};
      if (!accept(')')) {
        pos_ = open_pos;
        fail("missing ), unterminated subpattern");
      }
      --depth_;
      return add_node(out, {.kind = node_kind::group, .possessive = true, .group = -1, .child = body});
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
     * \brief Returns `true` if \p ch may start a group name in bytes mode.
     * \param[in] ch A character.
     * \return `true` if \p ch may start a group name.
     */
    static constexpr bool is_name_start(char ch)
    {
      return ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    }

    /*!
     * \brief Returns `true` if \p cp may start a text-mode group name (`str.isidentifier()`).
     *
     * Measured against UCD 16.0.0: `isidentifier()` is `XID_Start ∪ {U+005F}`, then `XID_Continue`.
     * \param[in] cp A code point.
     * \return `true` if \p cp may start a name.
     */
    static constexpr bool is_name_start_cp(char32_t cp)
    {
      return cp == U'_' || is_binprop_cp(binprop::XID_Start, cp);
    }

    /*!
     * \brief Fails a group name the way `re` does: quoting the name it read, at the name's start.
     *
     * As `re`: an empty name is `missing group name`, an unterminated one `missing >, unterminated name`,
     * and a bad character quotes the whole name it read.
     * \param[in] begin Offset of the name's first byte, which is where `re` reports.
     * \throws real::regex_error always.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_group_name(std::size_t begin) const
    {
      std::size_t close {begin};
      while (close < pattern_.size() && pattern_[close] != '>') {
        ++close;
      }
      if (close >= pattern_.size()) {
        throw regex_error("missing >, unterminated name", begin);
      }
      if (close == begin) {
        throw regex_error("missing group name", begin);
      }
      std::string message {"bad character in group name '"};
      for (std::size_t i = begin; i < close; ++i) {
        message += pattern_[i];
      }
      message += '\'';
      throw regex_error(message, begin);
    }

    /*!
     * \brief Fails a duplicate group name the way `re` does, naming it and both group numbers.
     * \param[in] begin    Offset of the offending name's first byte.
     * \param[in] end      One past its last byte.
     * \param[in] group    The capture number being defined now.
     * \param[in] previous The capture number that already carries this name.
     * \throws real::regex_error always.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_duplicate_group_name(std::size_t  begin,
                                                          std::size_t  end,
                                                          std::int32_t group,
                                                          std::int32_t previous) const
    {
      std::string message {"redefinition of group name '"};
      for (std::size_t i = begin; i < end; ++i) {
        message += pattern_[i];
      }
      message += "' as group ";
      message += std::to_string(group);
      message += "; was group ";
      message += std::to_string(previous);
      throw regex_error(message, begin);
    }

    /*!
     * \brief Parses a group name up to `>` and records it.
     *
     * Text mode: a Python identifier (`XID_Start ∪ {_}`, then `XID_Continue`), so `(?P<é>a)` is
     * accepted. Bytes mode: `[A-Za-z_][A-Za-z0-9_]*`, matching `re` on a bytes pattern. A
     * malformed UTF-8 sequence is "bad character in group name" at the bad byte — not a decode
     * error, which would be a different divergence. The name is stored as a byte span into the
     * pattern, so `group("é")` / `groupindex` resolve by the same octets.
     *
     * \param[in,out] out   The AST; the name is appended to \ref ast::names.
     * \param[in]     group The capture number this name refers to.
     * \throws real::regex_error on a bad character or a duplicate name.
     */
    constexpr void parse_group_name(ast&         out,
                                    std::int32_t group)
    {
      const std::size_t begin {pos_};
      // Bytes mode from the scope stack, not `bytes_` (see parse_property_table).
      if (has_flag(current_flags(), flags::bytes)) {
        if (eof() || !is_name_start(peek())) {
          fail_group_name(begin);
        }
        while (!eof() && (is_ascii_alnum(peek()) || peek() == '_')) {
          ++pos_;
        }
      }
      else {
        bool first {true};
        while (!eof() && peek() != '>') {
          const decoded_codepoint decoded {decode_codepoint_strict(pattern_, pos_)};
          if (!decoded.valid) {
            fail_group_name(begin);
          }
          const char32_t cp {static_cast<char32_t>(decoded.cp)};
          const bool     ok {first ? is_name_start_cp(cp) : is_binprop_cp(binprop::XID_Continue, cp)};
          if (!ok) {
            fail_group_name(begin);
          }
          pos_ += decoded.length;
          first = false;
        }
        if (first) {
          fail_group_name(begin);
        }
      }
      const std::size_t end {pos_};
      if (eof() || peek() != '>') {
        fail_group_name(begin);
      }
      ++pos_; // consume '>'

      for (const named_group& existing : out.names) {
        const std::string_view name    {pattern_.substr(begin, end - begin)};
        const auto             e_begin {static_cast<std::size_t>(existing.begin)};
        const auto             e_end   {static_cast<std::size_t>(existing.end)};
        if (pattern_.substr(e_begin, e_end - e_begin) == name) {
          fail_duplicate_group_name(begin, end, group, existing.group);
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
     * \param[in] backslash Offset of the `\` that opened the escape, forwarded so a truncated
     *                       `\xH` reports at the sequence's start the way `re` does.
     * \return The byte value, or -1 when the escape is not a single byte
     *         (the caller then handles `\d` `\w` `\s`, etc.).
     * \throws real::regex_error on a malformed `\x` escape.
     */
    constexpr std::int32_t parse_byte_escape(std::size_t backslash)
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
          // ECMAScript has no `\a`: an identity escape, the literal 'a' (in classes too).
          if (ecma_) { return 'a'; }
          return '\a';
        case 'x':
          {
            ++pos_;
            const std::int32_t high_nibble {hex_digit(backslash)};
            const std::int32_t low_nibble  {hex_digit(backslash)};
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
     * Octal escapes (`\0`, `\012`, a three-octal-digit run) become one byte (value & 0xff, as
     * `\xHH`). A decimal group number is a back-reference, unsupported.
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
      fail_unsupported("backreferences are not supported"); // a decimal group number = a back-reference
    }

    /*!
     * \brief Fails a truncated escape the way `re` does (`incomplete escape \x1`): quoting
     *        `pattern_[backslash_pos, pos_)`, reported at the backslash.
     *
     * \param[in] backslash_pos Offset of the `\` that opened the escape.
     * \throws real::regex_error always.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_incomplete_escape(std::size_t backslash_pos) const
    {
      std::string message {"incomplete escape "};
      for (std::size_t i = backslash_pos; i < pos_ && i < pattern_.size(); ++i) {
        message += pattern_[i];
      }
      throw regex_error(message, backslash_pos);
    }

    /*!
     * \brief Fails a bad character-class range the way `re` does, quoting it (`bad character range z-a`).
     *
     * Quotes the source text: `[\x7f-\x20]` names `\x7f-\x20`, where `re` prints `\x-\x`.
     *
     * \param[in] begin Offset of the range's first byte, which is where `re` reports.
     * \param[in] end   One past the range's last byte, as far as the caller had read.
     * \throws real::regex_error always.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_bad_range(std::size_t begin,
                                               std::size_t end) const
    {
      std::string message {"bad character range "};
      for (std::size_t i = begin; i < end && i < pattern_.size(); ++i) {
        message += pattern_[i];
      }
      throw regex_error(message, begin);
    }

    /*!
     * \brief Fails an escape REAL does not implement, naming it (`unsupported escape sequence \q`) and
     *        reporting at the backslash, as `re` does; tagged `unsupported`.
     *
     * \param[in] backslash Offset of the `\` that opened the escape.
     * \throws real::regex_error always.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_unsupported_escape(std::size_t backslash) const
    {
      std::string message {"unsupported escape sequence \\"};
      if (backslash + 1 < pattern_.size()) {
        message += pattern_[backslash + 1];
      }
      throw regex_error(message, backslash, error_kind::unsupported);
    }

    /*!
     * \brief Consumes one hexadecimal digit.
     * \param[in] backslash_pos Offset of the `\` that opened the escape, so a missing digit is
     *                           reported as a truncated escape at the sequence's start.
     * \return Its value in `[0, 15]`.
     * \throws real::regex_error if the next character is not a hex digit.
     */
    constexpr std::int32_t hex_digit(std::size_t backslash_pos)
    {
      if (eof()) {
        fail_incomplete_escape(backslash_pos);
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
      fail_incomplete_escape(backslash_pos);
    }

    /*!
     * \brief Decodes a `\uHHHH` (4 hex) or `\UHHHHHHHH` (8 hex) code-point escape (str only),
     *        or the braced form `\u{HHHHHH}` (1–6 hex) — the ECMAScript / regex-crate spelling,
     *        a synonym of `\x{…}` via \ref parse_braced_hex_scalar. `\U{…}` is not this form
     *        (`\U` stays 8 fixed digits).
     *
     * Rejects bytes mode, a surrogate, a value past U+10FFFF and incomplete hex. The backslash and
     * `u`/`U` are already consumed.
     *
     * \param[in] capital True for `\U` (8 digits), false for `\u` (4 digits or `\u{…}`).
     * \param[in] backslash Offset of the `\` that opened the escape, for a truncated-escape report.
     * \return The code point in `[0, 0x10FFFF]` (never a surrogate).
     */
    constexpr std::int32_t parse_unicode_codepoint(bool        capital,
                                                   std::size_t backslash)
    {
      if (bytes_) {
        fail("\\u and \\U escapes are not allowed in bytes patterns");
      }
      if (!capital && !eof() && peek() == '{') {
        ++pos_; // consume '{'
        return parse_braced_hex_scalar();
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
          fail_incomplete_escape(backslash);
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
     * \brief Decodes a braced hex scalar `HHHHHH}` (1–6 hex digits; the `{` already consumed), shared by
     *        `\N{U+…}`, `\x{…}` and `\u{…}`; rejects a surrogate or a value past U+10FFFF.
     *
     * \return The code point in `[0, 0x10FFFF]` (never a surrogate).
     * \throws real::regex_error on a missing digit run, an unterminated brace, a surrogate, or a value
     *         beyond U+10FFFF.
     */
    constexpr std::int32_t parse_braced_hex_scalar()
    {
      std::int32_t value {};
      int          count {};
      while (!eof() && count < 6) {
        const char   ch    {peek()};
        std::int32_t digit {-1};
        if (ch >= '0' && ch <= '9') {
          digit = ch - '0';
        }
        else if (ch >= 'a' && ch <= 'f') {
          digit = (ch - 'a') + 10;
        }
        else if (ch >= 'A' && ch <= 'F') {
          digit = (ch - 'A') + 10;
        }
        if (digit < 0) {
          break;
        }
        value = (value * 16) + digit;
        ++pos_;
        ++count;
      }
      if (count == 0) {
        fail("expected 1 to 6 hex digits in a braced hex escape");
      }
      if (eof() || peek() != '}') {
        fail("unterminated braced hex escape (expected '}')");
      }
      ++pos_; // consume '}'
      if (value >= 0xD800 && value <= 0xDFFF) {
        fail("invalid braced hex escape: surrogate code point");
      }
      if (value > 0x10FFFF) {
        fail("invalid braced hex escape: code point out of range");
      }
      return value;
    }

    /*!
     * \brief Decodes a `\N{U+XXXX}` escape (1–6 hex digits). `re` writes `\N{NAME}`; the Python binding
     *        rewrites a name to this form, so the engine sees only the scalar.
     *
     * Rejects bytes mode (as `re`'s `bad escape \N`) and a malformed `{U+…}`. The backslash and `N` are
     * already consumed.
     * \return The code point in `[0, 0x10FFFF]` (never a surrogate).
     */
    constexpr std::int32_t parse_named_codepoint()
    {
      if (bytes_) {
        fail("\\N escapes are not allowed in bytes patterns");
      }
      if (eof() || peek() != '{') {
        fail("expected '{' after \\N (\\N{U+XXXX})");
      }
      ++pos_; // consume '{'
      if (eof() || peek() != 'U') {
        fail("\\N{...} takes a U+XXXX code point; a character name is resolved by the Python binding");
      }
      ++pos_; // consume 'U'
      if (eof() || peek() != '+') {
        fail("expected '+' in \\N{U+XXXX}");
      }
      ++pos_; // consume '+'
      return parse_braced_hex_scalar();
    }

    /*!
     * \brief Decodes a `\x{XXXX}` escape (RE2/Perl; callers gate on `!is_ecma()`, ECMAScript spelling it
     *        `\u{...}`). Rejected in bytes mode, read from the scope stack (see parse_property_table). The
     *        backslash and `x` are already consumed.
     *
     * \return The code point in `[0, 0x10FFFF]` (never a surrogate).
     * \throws real::regex_error in bytes mode, or (via \ref parse_braced_hex_scalar) on a malformed or
     *         unterminated `{...}`, a surrogate, or a value beyond U+10FFFF.
     */
    constexpr std::int32_t parse_braced_hex_escape()
    {
      if (has_flag(current_flags(), flags::bytes)) {
        fail("\\x{...} escapes are not allowed in bytes patterns");
      }
      ++pos_; // consume '{'
      return parse_braced_hex_scalar();
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
     *
     * \param[in,out] out The AST the node is added to.
     * \param[in]     cp  The literal's code point.
     * \return The new node's index: a literal, or a foldable singleton class under `icase`.
     */
    constexpr std::int32_t emit_literal_codepoint(ast&         out,
                                                  std::int32_t cp)
    {
      if (is_icase()) {
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
      const std::size_t backslash {pos_}; // diagnostics report at the backslash, as `re` does
      ++pos_;                             // consume the backslash
      if (eof()) {
        fail("dangling backslash");
      }
      switch (peek()) {
        // A bare shorthand in text mode (not bytes, not re.A) is emitted as a match-time code-point
        // predicate (klass_cp): O(decode + range bsearch) per position, independent of the range count.
        // In bytes / ASCII mode shorthand_ranges() is empty and the flag is off -> the ASCII byte-NFA.
        case 'd':
        case 'D':
        case 'w':
        case 'W':
        case 's':
        case 'S': {
            const shorthand_spec sc {shorthand_class(peek(), text_shorthand())};
            ++pos_;
            return add_class_node(out, sc.set, sc.negated, shorthand_ranges(sc.ranges), text_shorthand());
          }
        // `\p{Name}` / `\P{Name}` / `\pX` — Unicode General_Category and Script property classes (text mode).
        case 'p':
        case 'P':
          return parse_unicode_property(out, peek() == 'P');
        // `\A \Z \z \< \>` are REAL anchors; under ecma they are identity escapes, the literal character
        // (a cased one folds under icase).
        case 'A':
          ++pos_;
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
        case 'z':
          // `\z` is an exact alias of `\Z`, as in Python 3.14.
          ++pos_;
          if (ecma_) {
            return emit_literal_codepoint(out, 'z'); // ecma: literal 'z' (identity escape), folds under icase
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
          return emit_literal_codepoint(out, parse_unicode_codepoint(false, backslash));
        case 'U':
          ++pos_;
          return emit_literal_codepoint(out, parse_unicode_codepoint(true, backslash));
        case 'N':
          ++pos_;
          return emit_literal_codepoint(out, parse_named_codepoint());
        // `\C` (RE2): exactly one raw byte. Gated on flags::bytes or flags::allow_raw_byte: elsewhere a span
        // could end mid-code-point and corrupt a binding that converts byte offsets to character offsets.
        case 'C':
          ++pos_;
          // Scope-stack reads (see parse_property_table); allow_raw_byte is never scoped either.
          if (!has_flag(current_flags(), flags::bytes) && !has_flag(current_flags(), flags::allow_raw_byte)) {
            fail_unsupported("\\C (raw-byte escape) requires flags::bytes or flags::allow_raw_byte -- it can split a UTF-8 codepoint");
          }
          return add_node(out, {.kind = node_kind::any, .raw_byte = true});
        default:
          {
            // `\` before a non-ASCII character is that character, one unit of the mode (the code point in
            // text mode, a byte under bytes): the atom the unescaped literal emits. Before
            // parse_byte_escape, whose byte node would not match the code point.
            if (static_cast<std::uint8_t>(peek()) >= 0x80U) {
              if (has_flag(current_flags(), flags::bytes)) {
                const std::int32_t raw {static_cast<std::uint8_t>(peek())};
                ++pos_;
                return emit_literal_codepoint(out, raw);
              }
              const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, pos_)};
              if (!decoded.valid) {
                fail("invalid UTF-8 byte in pattern");
              }
              pos_ += decoded.length;
              return emit_literal_codepoint(out, static_cast<std::int32_t>(decoded.cp));
            }
            // `\x{...}` (RE2/Perl) unless ecma, where Annex B keeps `\x` two-hex; anything else takes the
            // byte path of parse_byte_escape.
            if (peek() == 'x' && !is_ecma() && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] == '{') {
              ++pos_; // consume 'x'
              return emit_literal_codepoint(out, parse_braced_hex_escape());
            }
            const std::int32_t byte_value {parse_byte_escape(backslash)};
            if (byte_value < 0) {
              fail_unsupported_escape(backslash);
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
     * \param[in,out] ranges The class's non-ASCII code-point ranges; a Unicode
     *                   shorthand (`\d` `\w` `\s`, or a negated one) appends its ranges here in text mode.
     * \param[in,out] property_derived Set when a Unicode shorthand contributed, so the whole class is
     *                   emitted as a match-time `klass_cp` (text mode only).
     * \return A byte or code point (usable as a range endpoint), or -1 when the member
     *         was a whole set merged into \p klass.
     * \throws real::regex_error on invalid UTF-8 or an unsupported escape.
     */
    constexpr std::int32_t parse_class_item(char_class&               klass,
                                            std::vector<code_range>&  ranges,
                                            bool&                     property_derived)
    {
      const char ch {peek()};
      // Code-point mode decodes the whole code point as one member. Under bytes a bare high byte is an
      // ordinary member (below), as `re` on a bytes pattern and `std::regex<char>` read `[<C3>]`.
      if (!has_flag(current_flags(), flags::bytes) && static_cast<std::uint8_t>(ch) >= 0x80) {
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
      const std::size_t backslash {pos_}; // diagnostics report at the backslash
      ++pos_;                             // consume the backslash
      if (eof()) {
        fail("dangling backslash");
      }
      switch (peek()) {
        case 'd':
        case 'D':
        case 'w':
        case 'W':
        case 's':
        case 'S': {
            const shorthand_spec sc {shorthand_class(peek(), text_shorthand())};
            ++pos_;
            merge_property(klass, ranges, sc.set, sc.ranges, sc.negated, property_derived);
            return -1;
          }
        // `\p{Name}` / `\P{Name}` / `\pX` inside a class — a Unicode General_Category / Script property member.
        // Caret-negation `\p{^Name}` XORs into `negated` too, so `[\p{^L}]` == `[\P{L}]` here as well.
        case 'p':
        case 'P': {
            bool                         negated {peek() == 'P'};
            const property_table_result  table   {parse_property_table()};
            negated = negated != table.caret; // bool XOR without the int promotion misra rejects
            merge_unicode_property(klass, ranges, table.ranges, negated, property_derived);
            return -1;
          }
        case 'b':
          ++pos_;
          return 0x08; // backspace, only inside classes
        case 'u':
        case 'U':
          {
            const bool capital {peek() == 'U'};
            ++pos_;
            return parse_unicode_codepoint(capital, backslash);
          }
        case 'N':
          ++pos_;
          return parse_named_codepoint(); // \N{U+XXXX} is a valid class member (a code point)
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
          {
            // Inside a class every `\digit` is octal — there are no back-references in a class (re's
            // rule). Up to 3 octal digits; a value above 0o377 (255) is out of range, as in re. The
            // first non-octal digit ends the escape, so `[\18]` is `\x01` then a literal '8'.
            unsigned    value {};
            std::size_t taken {};
            while (taken < 3 && !eof() && peek() >= '0' && peek() <= '7') {
              value = (value * 8U) + static_cast<unsigned>(peek() - '0');
              ++pos_;
              ++taken;
            }
            if (value > 0xFFU) {
              fail("octal escape value out of range (\\0 to \\377)");
            }
            return static_cast<std::int32_t>(value);
          }
        case '8':
        case '9':
          fail("invalid escape (\\8 and \\9 are not octal and there are no back-references in a class)");
        default:
          {
            // As parse_escape's default, and what the unescaped member yields: a code point in text mode,
            // one byte under bytes (two escaped high bytes are two members, as in `re` and `std::regex`).
            // A code point is a range endpoint too (`[\à-\é]` is one range).
            if (static_cast<std::uint8_t>(peek()) >= 0x80U) {
              if (has_flag(current_flags(), flags::bytes)) {
                const std::int32_t raw {static_cast<std::uint8_t>(peek())};
                ++pos_;
                return raw;
              }
              const detail::decoded_codepoint decoded {detail::decode_codepoint_strict(pattern_, pos_)};
              if (!decoded.valid) {
                fail("invalid UTF-8 byte in character class");
              }
              pos_ += decoded.length;
              return static_cast<std::int32_t>(decoded.cp);
            }
            // The `\x{...}` gate of parse_escape.
            if (peek() == 'x' && !is_ecma() && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] == '{') {
              ++pos_; // consume 'x'
              return parse_braced_hex_escape();
            }
            const std::int32_t byte_value {parse_byte_escape(backslash)};
            if (byte_value < 0) {
              fail_unsupported_escape(backslash);
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
      ++pos_;                                      // consume '['
      const bool              negated {accept('^')};
      char_class              klass;
      std::vector<code_range> ranges;              // non-ASCII members (code-point mode); empty in bytes/ASCII-only classes
      bool                    property_derived {}; // a \w/\d/\s (text mode) contributed -> emit as klass_cp
      bool                    first            {true};
      // Bytes mode: a member >= 0x80 is a raw byte in the bitmap, so a bytes class is byte-for-byte a
      // std::basic_regex<char> class (the compat layer relies on it). Code-point mode: a one-point range.
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
        const std::int32_t range_start  {parse_class_item(klass, ranges, property_derived)};
        if (range_start < 0) {
          // A set item cannot be a range endpoint (`[\d-z]` raises, as in Python); a trailing '-]' is a
          // literal. The unparsed end endpoint is quoted as one character, two for an escape.
          if (!eof() && peek() == '-' && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] != ']') {
            std::size_t end {pos_ + 2};
            if (pattern_[pos_ + 1] == '\\' && pos_ + 2 < pattern_.size()) {
              ++end;
            }
            pos_ = item_pos;
            fail_bad_range(item_pos, end);
          }
          continue; // set item (e.g. \d): its bitmap and any Unicode ranges are already merged
        }
        // Possible range: 'x-y', where a trailing '-]' is a literal '-'.
        if (!eof() && peek() == '-' && pos_ + 1 < pattern_.size() &&
            pattern_[pos_ + 1] != ']') {
          ++pos_; // consume '-'
          const std::int32_t range_end {parse_class_item(klass, ranges, property_derived)};
          if (range_end < 0 || range_end < range_start) {
            const std::size_t end {pos_}; // captured BEFORE the rewind, or the quote would be empty
            pos_ = item_pos;
            fail_bad_range(item_pos, end);
          }
          add_range(range_start, range_end);
        }
        else {
          add_cp(range_start);
        }
      }
      return add_class_node(out, klass, negated, ranges, property_derived);
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
