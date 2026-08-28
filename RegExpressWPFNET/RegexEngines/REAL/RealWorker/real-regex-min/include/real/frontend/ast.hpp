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
    bool          raw_byte        {};                   //!< any: `\C` (RE2's raw-byte escape) — always 1 byte, bypasses UTF-8 even outside bytes mode. Parser requires flags::bytes (see `parse_escape`).
    bool          lazy            {};                   //!< repeat: prefer the shortest expansion.
    bool          possessive      {};                   //!< repeat: no give-back (`X*+`/`X++`/`X?+`/`X{n,m}+`). group: atomic `(?>...)` — same "no give-back" meaning, reused.
    look_dir      direction       {look_dir::ahead};    //!< lookaround: ahead `(?=`/`(?!` or behind `(?<=`/`(?<!`.
    std::int32_t  klass           {-1};                 //!< klass: index into \ref ast::classes.
    std::int32_t  min             {};                   //!< repeat: minimum count.
    std::int32_t  max             {-1};                 //!< repeat: maximum count (-1 = unbounded).
    std::int32_t  group           {-1};                 //!< group: capture number, -1 for `(?:...)`.
    std::int32_t  child           {-1};                 //!< First child (concat, repeat, alternation, group).
    std::int32_t  next            {-1};                 //!< Next sibling in the parent's child list.
    std::uint16_t effective_flags {};                   //!< Flag set in force where this node was parsed (see \ref flags). Stamped from the scope stack; carried for scoped-flag semantics.
  };

  /*!
   * \brief A parsed character class: its ASCII bitmap plus any non-ASCII code-point ranges. Bundling the
   *        two (rather than parallel side tables) makes them impossible to desynchronize.
   */
  struct class_def
  {
    char_class              ascii;                    //!< ASCII members as a bitmap (all 256 bytes in bytes mode); pre-negation.
    std::vector<code_range> ranges;                   //!< Non-ASCII code-point ranges (code-point mode only; empty otherwise).
    bool                    codepoint_predicate {};   //!< Emit as a match-time `klass_cp` (a Unicode shorthand `\w`/`\d`/`\s` in text mode), not the byte-NFA.
  };

  /*!
   * \brief Sorts \p ranges and merges overlapping / adjacent ones into a minimal, sorted set (the
   *        same set of code points, the fewest ranges). Used to keep folded / property classes compact.
   * \param[in] ranges The ranges to normalise; may be unsorted and overlapping.
   * \return The minimal sorted equivalent.
   */
  constexpr std::vector<code_range> coalesce_ranges(std::vector<code_range> ranges)
  {
    // Fast path: already sorted and disjoint (no overlap, no adjacency). The shorthand / property tables come
    // this way, so a bare `\w`/`\d`/`\p{…}` class skips the O(n log n) sort and the second allocation entirely —
    // which also keeps it within a static_regex's constexpr step budget (the sort of the large word table would
    // otherwise blow it). Only a genuinely mixed class (a predicate plus a literal/range, or two predicates)
    // falls through to the sort.
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
   * \brief Complements a set of code-point ranges within `[0x80, 0x10FFFF]` (used by negated classes
   *        and by an in-class `\W`/`\D`/`\S`). Input may be unsorted/overlapping; the gaps come sorted.
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
   * Resource caps used during parsing and later Thompson unrolling are
   * centralized in config.hpp (\ref max_repeat_count, \ref max_group_count,
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
   * A non-ASCII literal in text mode is parsed to its UTF-8 bytes wrapped in a `concat`
   * (`emit_codepoint_utf8`), so `é` is `concat(byte C3, byte A9)`. Two places need to recognise that
   * shape and treat it as the single atom it is -- the parser, so a POSSESSIVE quantifier over it is
   * Tier-1 eligible, and the compiler, so an UNBOUNDED one routes as a code-point class -- and they
   * ask here rather than each restating the test.
   *
   * The strict decode is the whole guard: it must consume every byte the chain holds, so a concat of
   * MORE than one code point (`(?:éé)`, `(?:ab)`) is refused. Treating those as one atom would change
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
        bytes_(has_flag(initial_flags, flags::bytes)),
        ecma_(has_flag(initial_flags, flags::ecma))
    {
      // The scope stack holds the flag set in force at each nesting level; the base is the
      // constructor's flags. A scoped group (?flags:...) pushes a modified copy for its body.
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
    std::vector<flags> flag_scopes_;      //!< Stack of the flag set in force per nesting level; the top is current. Replaces a global `verbose_` read so a scoped `(?x:...)` is honoured (see \ref current_flags).
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
     * \brief True when verbose mode (`re.X`) is in force here — read from the scope stack, so a
     *        scoped `(?x:...)` is honoured without a global flag read.
     * \return Whether \ref flags::verbose is active here.
     */
    [[nodiscard]] constexpr bool is_verbose() const
    {
      return has_flag(current_flags(), flags::verbose);
    }

    /*!
     * \brief True in the ECMAScript grammar. `flags::ecma` is not scopable, so the scope-stack
     *        base always carries it; reading it here keeps the flag-scope ratchet's global-read
     *        count at its terminal state (no new `ecma_` member reads).
     * \return Whether \ref flags::ecma is active.
     */
    [[nodiscard]] constexpr bool is_ecma() const
    {
      return has_flag(current_flags(), flags::ecma);
    }

    /*!
     * \brief True when icase (`re.I`) is in force at the current scope (a scoped `(?i:...)` honoured).
     * \return Whether \ref flags::icase is active here.
     */
    [[nodiscard]] constexpr bool is_icase() const
    {
      return has_flag(current_flags(), flags::icase);
    }

    /*!
     * \brief True when ascii (`re.A`) is in force at the current scope (a scoped `(?a:...)` honoured).
     * \return Whether \ref flags::ascii is active here.
     */
    [[nodiscard]] constexpr bool is_ascii_mode() const
    {
      return has_flag(current_flags(), flags::ascii);
    }

    /*!
     * \brief In verbose mode, consumes insignificant whitespace and `#` comments.
     *
     * No-op unless \ref is_verbose. Called only between tokens outside character
     * classes; escaped whitespace (`\ `) is read as a literal by the escape
     * parser, never reaching here.
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
     * \brief Like \ref fail, but tags the error as `unsupported` (well-formed but beyond REAL's linear
     *        engine — a backreference, `\p{…}`, a nested lookaround) so a binding can classify it without
     *        matching on the message text. Templated like \ref fail so it stays a valid `constexpr`.
     * \param[in] message The diagnostic text, reported at the current read offset.
     */
    template <typename = void>
    [[noreturn]] constexpr void fail_unsupported(const char* message) const
    {
      throw regex_error(message, pos_, error_kind::unsupported);
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
      // Stamp the flag set in force where this node was parsed (from the scope stack). Carried for
      // scoped-flag semantics; the compiler does not read it yet, so it does not change any program.
      node.effective_flags = static_cast<std::uint16_t>(current_flags());
      out.nodes.push_back(node);
      return static_cast<std::int32_t>(out.nodes.size()) - 1;
    }

    /*!
     * \brief Interns a class bitmap and appends a \ref node_kind::klass node.
     * \param[in,out] out     The AST being built.
     * \param[in]     klass      The class bitmap as written (before negation).
     * \param[in]     negated Whether the class was written negated.
     * \param[in]     ranges  Non-ASCII code-point ranges of the class (code-point mode; empty otherwise).
     * \param[in]     codepoint_predicate Emit as a match-time `klass_cp` (a text-mode Unicode shorthand),
     *                   not the byte-NFA.
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
     * \brief Merges an in-class shorthand (`\w \d \s` or a negated `\W \D \S`) into the class being
     *        built: its ASCII bitmap (or the complement, negated) plus, in text mode, its non-ASCII
     *        ranges (or their complement). Sets \p property_derived so the class is emitted as a
     *        match-time `klass_cp` (text mode only). In bytes / ASCII mode it stays a byte class.
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
        // Text: the gaps between the property's ranges. ASCII: shorthand_ranges is empty, so the
        // complement is the whole non-ASCII space (`\W` under re.A still matches é).
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
     * \brief `\s`'s ASCII-range (< 0x80) component for TEXT mode: `space_set()` (the ASCII-MODE set,
     *        `[ \t\n\r\f\v]`) plus `U+001C`-`U+001F` (FS/GS/RS/US). Needed because `shorthand_ranges`
     *        deliberately omits any wholly-ASCII range from `.ranges` ("already covered by the
     *        bitmap") — so for text mode, where `re`'s own `\s` DOES include FS/GS/RS/US (verified:
     *        `re.match(r"\s", "\x1c")` matches; `str.isspace()` agrees) but ASCII-mode `\s` does not
     *        (`re.match(r"(?a)\s", "\x1c")` does not match), the bitmap that "already covers" the
     *        ASCII range must itself differ by mode — `space_set()` alone is only correct for the
     *        ASCII-mode case. Found live by differential fuzzing (ASCII mode wrongly matching FS/GS/
     *        RS/US) and fixed once at `space_set()` itself before this second bug (text mode then
     *        losing them entirely) surfaced immediately in `test_classes.cpp`'s own regression suite —
     *        the two modes generate genuinely different ASCII bitmaps, not one shared one.
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
     * \brief Maps a shorthand letter to its \ref shorthand_spec. The single place the
     *        letter -> (set, range table, negation) fact lives; the atom ladder (parse_escape) and the
     *        class ladder (parse_class_item) share it, then each consumes the spec its own way (emit a
     *        class node vs merge into a class) -- the same shared-decode / divergent-use split as
     *        \ref decode_digit_escape.
     * \param[in] letter    The shorthand letter (`d D w W s S`).
     * \param[in] text_mode Whether this shorthand compiles as a text-mode code-point predicate (the
     *                      caller's own \ref text_shorthand) — only `\s`/`\S` need it: the ASCII-range
     *                      component of `\s` legitimately differs between ASCII mode (`[ \t\n\r\f\v]`)
     *                      and text mode (the same set plus `U+001C`-`U+001F`); `\w`/`\d` do not have
     *                      this divergence, so they ignore the parameter.
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
      // Unreachable: both call sites dispatch only on the six shorthand letters (see parse_escape /
      // parse_class_item). Kept as a structural fallback; never hit at run time (coverage-honest).
      return {.set = space_set(), .ranges = space_ranges, .negated = true};
    }

    /*!
     * \brief A loose-match key (lowercase, no `_`/`-`/space; UAX44-LM3-ish) built into a fixed buffer, so no
     *        heap or `<string>` is needed at parse time. A name longer than the buffer simply fails to match.
     */
    struct loose_buf
    {
      std::array<char, 64> data {}; //!< The normalised bytes, the first \ref len of which are meaningful.
      std::size_t          len  {}; //!< Bytes held in \ref data; a longer name is truncated and so matches nothing.

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
     * \brief Resolves a `\p{...}` property name to its code-point ranges, or fails with a clear error. An
     *        optional `gc=` / `sc=` / `scx=` (or `general_category=` / `script=` / `scriptextensions=`)
     *        prefix picks the namespace; a bare name tries General_Category, then Script, then a binary
     *        property (`\p{Alphabetic}`, no namespace of its own, same as PCRE2) -- `scx=` has no bare-name
     *        form (PCRE2: a bare name never means Script_Extensions, the explicit prefix is required). GC
     *        ranges come straight from the table; a Script's ranges are collected from the partition; a
     *        binary property's or a Script_Extensions' ranges come straight from their own table (both are
     *        NOT partitions -- a code point can satisfy several). The alias resolvers are the generated,
     *        loose-keyed `resolve_gc` / `resolve_script` (shared by `sc=` and `scx=` -- same script names,
     *        long or short UAX24 code) / `resolve_binprop`.
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
        // well-formed `\p{ns=...}` but a namespace REAL does not offer (a binding may delegate it).
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
        // Binary properties have no namespace of their own (like PCRE2): only a bare `\p{Name}` tries
        // one, never `\p{gc=Name}` / `\p{sc=Name}` with a name that failed to resolve in that explicit
        // namespace -- an explicit, misspelled namespace should fail, not silently fall through.
        const binprop bp {resolve_binprop(value_key.view())};
        if (bp != binprop::count) {
          const std::span<const code_range> t {binprop_ranges[static_cast<std::size_t>(bp)]};
          return {t.begin(), t.end()};
        }
        // `\p{Any}` is an engine-defined meta-property (RE2/Perl: "every code point"), not UCD data --
        // there is no generated table entry for it, so it is resolved here as the full code-point
        // range instead. ECMAScript-conforming (the spec's binary-property table lists `Any`; V8
        // accepts it), so this carries no ecma gate -- the fix benefits every dialect equally. Loose-
        // matched like every other bare name. `\p{gc=Any}` / `\p{sc=Any}` stay unsupported: Any is
        // neither a General_Category nor a Script, so an explicit namespace never reaches this branch
        // (gated by the enclosing `nk.empty()`).
        if (value_key.view() == "any") {
          return {{.lo = 0x0, .hi = 0x10FFFF}};
        }
      }
      // well-formed `\p{Name}` but a property REAL does not yet tabulate (a script/category REAL lacks,
      // or an unknown/misspelled binary property or Script_Extensions name): unsupported, so a binding
      // can delegate it.
      fail_unsupported("unsupported Unicode property in \\p{...} (General_Category, Script, "
                       "Script_Extensions and the standard binary properties are built in)");
    }

    /*!
     * \brief The result of \ref parse_property_table — the property's code-point ranges, plus whether a
     *        leading `\p{^...}` caret was stripped (RE2/Perl negation-by-caret, e.g. `\p{^L}` == `\P{L}`).
     *        `caret` is XORed into the caller's own negation flag so the caret composes with `\P` /
     *        `[^...]` instead of overriding them.
     */
    struct property_table_result
    {
      std::vector<code_range>  ranges; //!< The resolved property's code-point ranges (never caret-adjusted).
      bool                     caret;  //!< True when a leading `^` (native dialects only) was stripped from the name.
    };

    /*!
     * \brief Rejects bytes mode, consumes the `p`/`P` and the `{Name}` (or single letter), strips a leading
     *        `^` caret-negation (native dialects only), and resolves the remaining name to the property's
     *        code-point ranges. Shared by the out-of-class atom and the in-class merge. On entry `pos_` is on
     *        the `p`/`P`; on return it is just past the name (caret and all).
     * \return The resolved ranges and whether a caret-negation was stripped.
     */
    constexpr property_table_result parse_property_table()
    {
      // Read bytes-mode from the scope stack, not the global `bytes_` member (the flag-scope ratchet): bytes is
      // never scoped, so this equals `bytes_` while keeping the parser's global-read count flat.
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
      // `\p{^Name}` is RE2/Perl caret-negation (`\p{^L}` == `\P{L}`), not ECMAScript (a SyntaxError under
      // V8) -- so the strip is gated to the native dialects only. Under ecma the `^` is left in `name`
      // and falls through to `resolve_property`, which fails it with the existing "unsupported Unicode
      // property" message -- rejection is automatic, no new error path. The caret sits ahead of any
      // namespace, so `\p{^gc=L}` / `\p{^Latin}` strip first and then negate whatever the namespace
      // resolves.
      bool caret {false};
      if (!is_ecma() && !name.empty() && name.front() == '^') {
        caret = true;
        name.remove_prefix(1);
      }
      return {.ranges = resolve_property(name), .caret = caret};
    }

    /*!
     * \brief Splits a property's ranges into its ASCII bitmap (< 0x80) and its non-ASCII ranges. Unconditional:
     *        unlike `\w`, `flags::ascii` (`re.A`) does not restrict a Unicode property, so both parts are always
     *        used (bytes mode having already been rejected).
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
     * \brief Parses `\p{Name}` / `\P{Name}` / `\pX` (outside a class) into a negatable Unicode code-point class
     *        (`klass_cp`), reusing the same match-time mechanism as `\w`. Negation is the class-node flag, as for
     *        `\W`, XORed with a caret-negation `\p{^Name}` stripped by \ref parse_property_table (so
     *        `\P{^L}` negates twice back to `\p{L}`, same as `\P{...}` on an already-negated property would).
     *        `pos_` is on the letter after `\`; `negated` distinguishes `\P` from `\p`.
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
     * \brief Merges a `\p{Name}` / `\P{Name}` property into the character class being built (the in-class form) —
     *        the un-gated twin of \ref merge_property — `flags::ascii` never restricts it, so it always uses the
     *        property's own non-ASCII ranges. A negated `\P{...}` merges the complement (the inverted ASCII bitmap
     *        plus the gaps between the non-ASCII ranges), exactly as `\W` negates in a class; an enclosing
     *        `[^...]` then negates the whole class on top (so `[^\P{L}]` == `[\p{L}]`). bytes mode is already
     *        rejected by \ref parse_property_table.
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
     * Also intercepts `\Q...\E` literal quoting here (RE2/Perl syntax, `!is_ecma()` only — under
     * ecma `\Q` keeps falling through to the rejected unknown escape): the span emits a SEQUENCE of
     * literal atoms, not one atom, so it cannot live in \ref parse_atom. A quantifier after `\E`
     * binds to the span's LAST character (`\Qab\E+` == `ab+`, libre2-measured): all-but-last chain
     * bare and the last atom re-enters the loop's normal quantifier path. An empty `\Q\E` is
     * grammar-invisible (libre2-measured `a\Q\E+` == `a+`): a quantifier after it re-binds to the
     * PREVIOUS atom — the `prev` tracker exists to re-chain that re-quantified atom — and with no
     * previous atom the next iteration fails ("nothing to repeat"), matching RE2's "no argument for
     * repetition operator".
     *
     * \param[in,out] out The AST being built.
     * \return The index of a concat node, a single atom, or an empty node.
     */
    constexpr std::int32_t parse_sequence(ast& out)
    {
      std::int32_t first {-1};
      std::int32_t last  {-1};
      std::int32_t prev  {-1}; // predecessor of `last` (-1: none): the re-chain point after an empty \Q\E
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
     * \brief What \ref parse_quoted_span emitted: a bare pre-chained all-but-last prefix
     *        (`head`..`head_tail`, -1 when the span has fewer than two characters) plus the span's final
     *        atom `last` (-1 when the span is empty) — the caller's quantifier target.
     */
    struct quoted_span
    {
      std::int32_t head      {-1}; //!< First atom of the all-but-last prefix chain (-1: none).
      std::int32_t head_tail {-1}; //!< Last atom of the prefix chain (-1: none).
      std::int32_t last      {-1}; //!< The span's final atom (-1: empty span).
    };

    /*!
     * \brief Scans a `\Q...\E` literal span (the `\Q` is already consumed) and emits its characters
     *        as literal atoms — the same emission as \ref parse_atom's default (whole code point per
     *        atom in text mode, single byte in bytes mode, icase folding via
     *        \ref emit_literal_codepoint), libre2-measured semantics:
     *
     * - The span ends at the exact two-character `\E` (consumed) or at the end of the pattern
     *   (an unterminated `\Q` quotes to the end).
     * - Everything inside is literal — metacharacters, `|`, `)`, whitespace (even in verbose mode;
     *   RE2 has no `(?x)` so this is REAL's own call: a quoted span protects its spaces), and a
     *   backslash NOT followed by `E` (so `\Qa\Qb\E` is the literal `a\Qb` and a trailing `\Qa\` is
     *   the literal `a\` — the "dumb scan": no escape processing, no nesting).
     * - All atoms except the last are chained bare; the last is left unchained so the caller can
     *   apply a following quantifier to it alone (`\Qab\E+` == `ab+`).
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
        // Read bytes-mode from the scope stack, not the global `bytes_` member (the flag-scope
        // ratchet; bytes is never scoped, so this equals `bytes_` — same precedent as `\C`).
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
      // (?U) ungreedy (RE2): swap the default greediness — a bare quantifier becomes lazy and the
      // explicit '?' re-inverts back to greedy (measured vs libre2 11.0.0: `(?U)a+` on "aaa" matches
      // "a", `(?U)a+?` matches "aaa"). Read from the scope stack, so `(?U:...)` scopes, `(?-U:...)`
      // removal and the flags::ungreedy constructor seed all work; resolved here at parse time into
      // node.lazy — the compiler and VM are unchanged.
      const bool explicit_q {accept('?')};
      const bool lazy       {has_flag(current_flags(), flags::ungreedy) ? !explicit_q : explicit_q};
      // X*+/X++/X?+/X{n,m}+ — native dialect only, mutually exclusive with lazy. ECMAScript has no
      // possessive quantifiers, so under ecma the '+' stays unconsumed and hits the multiple-repeat
      // check below (SyntaxError, agreeing with V8 and both std libraries). Under (?U) a bare
      // quantifier is lazy, so `(?U)a++` fails "multiple repeat" — in agreement with libre2, which
      // rejects it too ("bad repetition operator", RE2 has no possessives; measured 2026-07-17).
      const bool possessive {!is_ecma() && !lazy && accept('+')};
      if (!eof()) {
        const char   ch          {peek()};
        std::int32_t ignored_min {};
        std::int32_t ignored_max {-1};
        if (ch == '*' || ch == '+' || ch == '?' ||
            (ch == '{' && try_parse_braces(ignored_min, ignored_max))) {
          fail("multiple repeat");
        }
      }
      // A POSSESSIVE quantifier over a non-ASCII literal was rejected outright -- `é++`, `あ++`,
      // `é*+`, `é{2,}+` all threw "possessive/atomic over a compound body", while the SAME character
      // written as a class (`[é]++`) compiled, and so did `(?i)é++`, since the fold already promotes
      // the literal to a class here. The bodies are the same language; only their AST shape differed.
      // Tier-1 eligibility is tested on the node KIND, and a non-ASCII literal is a concat of bytes,
      // so it could never qualify. Promoted to the one-member code-point class it is equivalent to,
      // it qualifies -- exactly the route `[é]++` already takes, reached by the same precedent the
      // icase promotion set a few lines above.
      //
      // Scoped to possessive on purpose. An unbounded ordinary quantifier is handled in the compiler
      // (emit_unbounded_body), and a BOUNDED one is deliberately left alone: `é{2}` emits bytes, which
      // is what lets a literal run route as an exact literal.
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
     * \brief Returns `true` if \p ch is an ASCII letter (`A`–`Z`, `a`–`z`).
     *
     * Used to tell an unknown flag letter (`z`, `u`, `L`) from a terminator (`:`, `)`, `-`)
     * after a flag run — CPython `_parse_flags` uses `str.isalpha()` for the same split.
     * The parser peeks bytes, so this is ASCII-only; a non-ASCII lead byte is not a flag.
     * \param[in] ch A character.
     * \return `true` if \p ch is an ASCII letter.
     */
    static constexpr bool is_ascii_letter(char ch)
    {
      return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
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
     * \brief Fails with "unknown flag" if the next byte is an ASCII letter that is not a flag.
     *
     * CPython `_parse_flags` makes this diagnosis, and it takes precedence over the caller's
     * terminator check (misplaced global / missing flag after `-`). Call only after a flags
     * group has started (a consumed flag letter, or `-`): the leading-global prefix must
     * backtrack on `(?P` / `(?#` rather than treat `P` as an unknown flag.
     */
    constexpr void fail_if_unknown_flag()
    {
      if (!eof() && is_ascii_letter(peek())) {
        fail("unknown flag");
      }
    }

    /*!
     * \brief \p value with \p bit cleared. The intermediate cast matches the enum's
     *        `std::uint16_t` underlying type — a `std::uint8_t` here (the pre-widening
     *        vestige) would silently drop `flags::ungreedy` (512) from every scope.
     * \param[in] value The flag set to clear from.
     * \param[in] bit   The flag to clear.
     * \return \p value without \p bit.
     */
    static constexpr flags without(flags value,
                                   flags bit)
    {
      return flags_without(value, bit); // one implementation of the width rule, in program.hpp
    }

    /*!
     * \brief Consumes a leading global-flags group -- `(?imsxaU)`, or `(?flags-flags)` with a removal
     *        suffix -- if present. The accepted letters are `i m s x a U` (\ref is_flag_letter).
     *
     * Like Python (3.11+), global flags are only legal at the very start of the
     * pattern; later occurrences are rejected in \ref parse_group. RE2 additionally
     * permits an optional `-removed` suffix (e.g. `(?i-s)`, or a pure `(?-s)`) that
     * clears flags from the base scope for the rest of the pattern — this mirrors
     * the added/`-`/removed parse in \ref parse_group's scoped-flags branch
     * (`(?flags-flags:...)`), minus its trailing `:` (a global prefix has none).
     *
     * \param[in,out] out Receives the added letters into \ref ast::inline_flags and the removed ones
     *                    into \ref ast::inline_removed. Two fields rather than one net set because
     *                    \ref ast::inline_flags is OR-ed across calls and so cannot carry a removal;
     *                    the caller applies them in order, adding then clearing.
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
          // '-' right after (? is only ever a flags construct (global or scoped) — see
          // parse_group's dispatch, which fails the same way for the scoped form. No other
          // (?...) construct starts with a dash, so this is a hard failure, not a backtrack.
          fail("missing flag after '-'");
        }
        any_removed = true;
      }
      if ((!any_letter && !any_removed) || !accept(')')) {
        pos_ = saved_pos; // some other (?...) construct, or a scoped (?flags-flags:...): let parse_group decide
        return false;
      }
      out.inline_flags   = out.inline_flags | found;
      out.inline_removed = out.inline_removed | removed;
      // A leading (?flags) group sets the base scope, so the rest of the pattern is parsed under it —
      // verbose affects tokenization, icase/ascii affect literal folding and the \w\d\s tables. This
      // mirrors the constructor flags, which seed the same base scope. The optional -removed clears
      // flags from that same base scope (RE2 semantics: (?i-s) enables i and disables s from here on).
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
     * Unsupported extensions (lookaround, backreferences, atomic groups,
     * scoped inline flags) fail with a message naming the feature. Under
     * `flags::ecma` the native-only constructs `(?#...)`, `(?P<name>` and the
     * atomic group `(?>...)` fail as "unknown extension" — the ECMAScript
     * grammar has no such groups (possessive quantifiers are gated the same
     * way at their parse site). Nesting beyond \ref max_nesting_depth is
     * rejected.
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
        if (!is_ecma() && accept('#')) { // (?#...) comments are native-dialect; under ecma: unknown extension
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
        else if (!is_ecma() && accept('P')) { // (?P<name>/(?P= are native-dialect; under ecma: unknown extension
          if (accept('<')) {
            group = new_group(out, open_pos);
            parse_group_name(out, group);
          }
          else if (!eof() && peek() == '=') {
            fail_unsupported("named backreferences are not supported");
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
        else if (!is_ecma() && !eof() && peek() == '>') { // atomic (?>...) is native-dialect; under ecma: unknown extension
          return parse_atomic_group(out, open_pos);
        }
        else if (!eof() && peek() == '(') {
          fail("conditional groups are not supported");
        }
        else if (!eof() && (is_flag_letter(peek()) || peek() == '-')) {
          // (?flags:...) / (?-flags:...) / (?flags-flags:...) — a scoped-flags group. Parse the
          // added flags, an optional '-' and the removed flags. An unknown letter is "unknown
          // flag" (fail_if_unknown_flag), not the terminator diagnostics below.
          const flags added {consume_flag_letters()};
          fail_if_unknown_flag();
          flags removed     {flags::none};
          if (accept('-')) {
            removed = consume_flag_letters();
            fail_if_unknown_flag();
            if (removed == flags::none) {
              fail("missing flag after '-'");
            }
          }
          if (!accept(':')) {
            // An unscoped (?flags) is only legal at the very start of the pattern (consumed by
            // parse_global_flags_prefix); anything else here is a misplaced global-flags group.
            fail("global flags not at the start of the expression");
          }
          // Every inline flag (i m s x a U) is honoured per scope: verbose changes tokenization, icase/
          // ascii govern folding and the \w\d\s tables, dotall the dot, multiline the ^/$ anchors,
          // ungreedy the default quantifier greediness — all read from the scope stack. The added set
          // is applied and the removed set cleared for the body.
          flag_scopes_.push_back(without(current_flags() | added, removed));
          scoped_flags = true; // group stays non-capturing (-1)
        }
        else {
          fail("unknown extension");
        }
      }
      else {
        group = new_group(out, open_pos);
      }
      const std::int32_t body {parse_alternation(out)};
      if (scoped_flags) {
        flag_scopes_.pop_back(); // the scoped flags apply only to the body just parsed
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
     * Builds a \ref node_kind::group node with `possessive = true` and `group = -1`
     * (atomic groups are never capturing at their own level, exactly like `(?:...)`;
     * a numbered capture group written inside one still gets its own number and
     * stays visible after the atomic group closes — the parser does not special-case
     * this, since it never restricts capture numbering inside the body). Compile-time
     * linearity/support restrictions (deterministic-body tiers) are enforced later by
     * the compiler, not here — this function only builds the tree.
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
      fail_unsupported("backreferences are not supported"); // a decimal group number = a back-reference
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
     * \brief Decodes a `\uHHHH` (4 hex) or `\UHHHHHHHH` (8 hex) code-point escape (str only),
     *        or the braced form `\u{HHHHHH}` (1–6 hex) — the ECMAScript / regex-crate spelling,
     *        a synonym of `\x{…}` via \ref parse_braced_hex_scalar. `\U{…}` is not this form
     *        (`\U` stays 8 fixed digits).
     *
     * Rejected with clear messages: byte mode (no code-point meaning; the constructor `bytes_`
     * member, matching `\u`/`\U` — not the scoped-flag read `\x{…}` uses), a surrogate
     * (U+D800–U+DFFF), beyond U+10FFFF, or incomplete hex. The backslash and `u`/`U` are
     * already consumed; this reads the hex digits, or `{` then the shared braced reader.
     *
     * \param[in] capital True for `\U` (8 digits), false for `\u` (4 digits or `\u{…}`).
     * \return The code point in `[0, 0x10FFFF]` (never a surrogate).
     */
    constexpr std::int32_t parse_unicode_codepoint(bool capital)
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
     * \brief Decodes a braced hex scalar `HHHHHH}` (1–6 hex digits, then the closing `}`) — the code-
     *        point reader shared by `\N{U+XXXX}` (after its own `U+` prefix), `\x{XXXX}` (after its
     *        own bytes-mode check, see \ref parse_braced_hex_escape), and `\u{XXXX}` (after
     *        \ref parse_unicode_codepoint's bytes-mode check and opening `{`). The opening `{` is already
     *        consumed by the caller; this reads the hex digits, the closing `}`, and rejects a
     *        surrogate (U+D800–U+DFFF) or a value beyond U+10FFFF — the same code-point range `\u`/`\U`
     *        enforce (Python semantics).
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
     * \brief Decodes a `\N{U+XXXX}` named-code-point escape (1–6 hex digits) — the same code-point path
     *        as `\u`/`\U`, spelled by its U+ scalar value. `re` writes `\N{NAME}` for the *name*; the
     *        Python binding rewrites a name to this `U+XXXX` form before parsing, so the engine only ever
     *        sees the scalar. A C++ caller writes `\N{U+XXXX}` directly.
     *
     * Rejected with clear messages: byte mode (no code-point meaning ≡ `re`'s `bad escape \N`), a missing
     * or malformed `{U+…}`; \ref parse_braced_hex_scalar rejects a surrogate or a value beyond U+10FFFF.
     * The backslash and `N` are already consumed.
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
     * \brief Decodes a `\x{XXXX}` braced code-point escape — RE2/Perl syntax (ECMAScript spells this
     *        `\u{...}` instead, so every caller gates on `!is_ecma()` before reaching here). Rejected in
     *        bytes mode, like `\u`/`\U`/`\N` (no code-point meaning there) — read from the scope stack,
     *        not the global `bytes_` member (the flag-scope ratchet: bytes is never scoped, so this
     *        equals `bytes_` while keeping the parser's global-read count flat; same precedent as `\C`
     *        above). Shares its digit-loop / surrogate / overflow validation with `\N{U+XXXX}` via
     *        \ref parse_braced_hex_scalar — this function only adds the bytes-mode check and the
     *        opening `{`. The backslash and `x` are already consumed by the caller.
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
      ++pos_; // consume the backslash
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
        case 'z':
          // `\z` is an exact alias of `\Z` (end of the text, no MULTILINE interaction) — Python 3.14
          // added it with that meaning. Same anchor, so it is byte-identical to `\Z`.
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
          return emit_literal_codepoint(out, parse_unicode_codepoint(false));
        case 'U':
          ++pos_;
          return emit_literal_codepoint(out, parse_unicode_codepoint(true));
        case 'N':
          ++pos_;
          return emit_literal_codepoint(out, parse_named_codepoint());
        // `\C` — RE2's raw-byte escape hatch: match exactly one byte, bypassing UTF-8 entirely (RE2 does
        // this even under its default UTF-8 mode). Gated on flags::bytes OR flags::allow_raw_byte: outside
        // either, a \C span can land mid-codepoint, which is well-formed on a byte-offset API (RE2 behaves
        // identically) but corrupts a binding that converts byte offsets to character offsets (e.g. the
        // Python layer) -- rejecting it there (where every offset is already byte-native under flags::bytes,
        // or explicitly opted into via allow_raw_byte by a byte-offset-native consumer like real::compat::re2)
        // keeps every char-offset REAL-native surface codepoint-clean by construction, matching the
        // strict-dot precedent (see divergences.dox). allow_raw_byte widens the *gate* only -- \C itself
        // still always consumes exactly one raw byte, same as under flags::bytes.
        case 'C':
          ++pos_;
          // Read bytes-mode from the scope stack, not the global `bytes_` member (the flag-scope ratchet):
          // bytes is never scoped, so this equals `bytes_` while keeping the parser's global-read count flat.
          // allow_raw_byte is never scoped either (a constructor-only opt-in, like bytes).
          if (!has_flag(current_flags(), flags::bytes) && !has_flag(current_flags(), flags::allow_raw_byte)) {
            fail_unsupported("\\C (raw-byte escape) requires flags::bytes or flags::allow_raw_byte -- it can split a UTF-8 codepoint");
          }
          return add_node(out, {.kind = node_kind::any, .raw_byte = true});
        default:
          {
            // `\x{...}` is RE2/Perl's braced code-point escape (ECMAScript spells this `\u{...}`
            // instead — Annex B has no braced `\x`, so under ecma `\x` keeps its two-hex meaning).
            // Gated `!is_ecma()`, mirroring `\u`/`\U` above (l.1770/1772). Anything else — ecma, `\x`
            // not followed by `{`, or any other escaped char — falls through unchanged to the
            // existing `\xHH` / octal / punctuation byte path via parse_byte_escape below.
            if (peek() == 'x' && !is_ecma() && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] == '{') {
              ++pos_; // consume 'x'
              return emit_literal_codepoint(out, parse_braced_hex_escape());
            }
            const std::int32_t byte_value {parse_byte_escape()};
            if (byte_value < 0) {
              fail_unsupported("unsupported escape sequence");
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
     * \return A single byte (usable as a range endpoint), or -1 when the member
     *         was a whole set merged into \p klass.
     * \throws real::regex_error on a non-ASCII member or an unsupported escape.
     */
    constexpr std::int32_t parse_class_item(char_class&               klass,
                                            std::vector<code_range>&  ranges,
                                            bool&                     property_derived)
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
            // A non-ASCII code point is now a valid class member (code-point mode); `parse_unicode_codepoint`
            // already rejects `\u`/`\U` in bytes mode, so a class in bytes mode still has ASCII-only members.
            return parse_unicode_codepoint(capital);
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
            // Mirrors the outside-class `\x{...}` gate in parse_escape: RE2/Perl braced code point,
            // `!is_ecma()`, else the existing `\xHH` byte path below (parse_byte_escape) is unchanged.
            if (peek() == 'x' && !is_ecma() && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] == '{') {
              ++pos_; // consume 'x'
              return parse_braced_hex_escape();
            }
            const std::int32_t byte_value {parse_byte_escape()};
            if (byte_value < 0) {
              fail_unsupported("unsupported escape sequence");
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
        const std::int32_t range_start  {parse_class_item(klass, ranges, property_derived)};
        if (range_start < 0) {
          // A set item cannot be a range ENDPOINT either. Falling through to the next iteration left
          // the '-' to be read as a literal member, so `[\d-z]` quietly became `\d` plus '-' plus 'z'
          // and matched "-" -- while the mirror case `[a-\d]` already failed below. Python raises on
          // both; this half of the rule was simply missing. A trailing '-]' stays a literal, as there.
          if (!eof() && peek() == '-' && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] != ']') {
            pos_ = item_pos;
            fail("bad character range");
          }
          continue; // set item (e.g. \d): its bitmap and any Unicode ranges are already merged
        }
        // Possible range: 'x-y', where a trailing '-]' is a literal '-'.
        if (!eof() && peek() == '-' && pos_ + 1 < pattern_.size() &&
            pattern_[pos_ + 1] != ']') {
          ++pos_; // consume '-'
          const std::int32_t range_end {parse_class_item(klass, ranges, property_derived)};
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
