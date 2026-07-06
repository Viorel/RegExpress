/*!
 * \file program.hpp
 * \brief Compiled form of a pattern and the public flags / error types.
 *
 * Defines the NFA instruction set executed by the engine, the heap-allocated
 * program the compiler produces, the non-owning view the engine runs over,
 * the compilation \ref real::flags, and \ref real::regex_error (thrown on an
 * invalid pattern).
 */
#ifndef REAL_PROGRAM_HPP
#define REAL_PROGRAM_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/std/regex.hpp>).

#include "real/version.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "real/core/charclass.hpp"

namespace real {

  /*!
   * \brief Sentinel for "no position" / unset capture slot (akin to std::string::npos).
   */
  inline constexpr std::size_t npos {std::numeric_limits<std::size_t>::max()};

  /*!
   * \brief Compilation flags, mirroring Python's `re.I`, `re.M` and `re.S`.
   *
   * Combinable with \ref operator|. Case folding is ASCII-only, consistent with
   * the library's character-class semantics.
   */
  enum class flags : std::uint8_t
  {
    none      = 0,        //!< No flags.
    icase     = 1,        //!< Case-insensitive (ASCII).
    multiline = 2,        //!< `^` and `$` also match at line boundaries.
    dotall    = 4,        //!< `.` also matches `\n`.
    bytes     = 8,        //!< Binary mode: `.` and `[^…]` match raw bytes, not codepoints.
    verbose   = 16,       //!< Verbose mode (`re.X`): ignore unescaped whitespace and `#` comments outside classes.
    ecma = 32,            //!< ECMAScript compatibility: `$` (no multiline) matches only at the very end (not before a final `\n`, the Python default), AND `.` (no dotall) also excludes `\r` (ECMAScript excludes `\n` and `\r`; the multi-byte U+2028/U+2029 have no byte-level effect).
    ascii = 64,           //!< ASCII mode (`re.A`): `\d \w \s \b` stay ASCII and icase folds ASCII only, even in text mode. `.`, explicit classes and UTF-8 literals stay code-point-aware.
    dollar_endonly = 128, //!< `$` (no multiline) matches only at the very end of the text, never before a final `\n` — the Rust/`\z` semantics. Unlike \ref flags::ecma this touches `$` ONLY, leaving `.` at the Python default. Used by the Rust binding for drop-in parity.
  };

  /*!
   * \brief Bitwise-OR of two flag sets.
   * \param[in] lhs First flag set.
   * \param[in] rhs Second flag set.
   * \return The union of \p lhs and \p rhs.
   */
  constexpr flags operator|(flags lhs,
                            flags rhs)
  {
    return static_cast<flags>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
  }

  /*!
   * \brief Bitwise-AND of two flag sets.
   * \param[in] lhs First flag set.
   * \param[in] rhs Second flag set.
   * \return The intersection of \p lhs and \p rhs.
   */
  constexpr flags operator&(flags lhs,
                            flags rhs)
  {
    return static_cast<flags>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
  }

  /*!
   * \brief Tests whether \p flag is set in \p value.
   * \param[in] value The flag set to query.
   * \param[in] flag  The single flag to look for.
   * \return `true` if \p flag is present in \p value.
   */
  constexpr bool has_flag(flags value,
                          flags flag)
  {
    return (value & flag) != flags::none;
  }

  /*!
   * \brief Exception thrown for an invalid pattern (or one exceeding a limit).
   *
   * In a constexpr context (`static_regex`), reaching the throw is a
   * compile-time error, with the message appearing in the diagnostic trace.
   */
  //! \brief Whether a rejected pattern is malformed (`syntax`) or well-formed but beyond REAL's linear engine
  //!        (`unsupported`: a backreference, `\p{…}`, a nested/unbounded lookaround). The distinction is a
  //!        stable, machine-readable classification the C ABI exposes so a binding never has to grep `what()`.
  enum class error_kind : std::uint8_t
  {
    syntax,
    unsupported,
  };

  class regex_error : public std::exception
  {
  public:

    /*!
     * \brief Builds the error.
     * \param[in] message  Human-readable cause.
     * \param[in] position Byte offset in the pattern where the error was found.
     * \param[in] kind     Whether the pattern is malformed or merely unsupported (default `syntax`).
     */
    regex_error(const std::string& message,
                std::size_t        position,
                error_kind         kind = error_kind::syntax)
      : message_("regex_error at " + std::to_string(position) + ": " + message),
        position_(position),
        kind_(kind)
    {}

    /*!
     * \brief Whether the pattern is malformed (`syntax`) or well-formed but unsupported by REAL.
     */
    [[nodiscard]] error_kind kind() const noexcept
    {
      return kind_;
    }

    /*!
     * \brief Returns the formatted error message (with position).
     */
    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

    /*!
     * \brief Returns the byte offset in the pattern where the error was found.
     */
    [[nodiscard]] std::size_t position() const noexcept
    {
      return position_;
    }

  private:

    std::string message_;  //!< Formatted message returned by what().
    std::size_t position_; //!< Offset in the pattern text.
    error_kind  kind_;     //!< Malformed (syntax) vs unsupported-by-REAL.
  };

  namespace detail {

    //! \brief An inclusive code-point range `[lo, hi]`. Shared by character classes (ast.hpp) and the
    //!        generated Unicode property / fold tables; lives here so those low-level headers need not
    //!        pull in the parser.
    struct code_range
    {
      std::uint32_t lo {}; //!< First code point (inclusive).
      std::uint32_t hi {}; //!< Last code point (inclusive).
    };

    //! \brief A match-time code-point class for the `klass_cp` opcode: an ASCII bitmap for code points
    //!        `< 0x80` plus a slice of sorted non-ASCII ranges (indexing the program's flat `cp_ranges`
    //!        buffer). It is the already-effective set (any `\W`/`[^…]` negation is materialised at
    //!        compile time). Unlike the byte-NFA `klass`, the ranges are kept and binary-searched at
    //!        match time — O(log ranges) per position, independent of the range count.
    struct cp_class
    {
      char_class    ascii;          //!< Members `< 0x80`.
      std::uint32_t range_begin {}; //!< First range in the program's `cp_ranges` buffer.
      std::uint32_t range_count {}; //!< Number of ranges belonging to this class.
    };

    /*!
     * \brief NFA instruction opcodes executed by the Pike VM.
     */
    enum class opcode : std::uint8_t
    {
      byte,              //!< Consume one byte equal to arg8; fall through to pc+1.
      klass,             //!< Consume one byte in classes[arg16]; fall through to pc+1.
      klass_cp,          //!< Consume one code point tested against cp_classes[arg16] (decode + range bsearch); enters a 3-instr continuation chain via a computed skip. See pike.hpp.
      split,             //!< Epsilon-branch to x (preferred) and y.
      jump,              //!< Epsilon-jump to x.
      save,              //!< Store current position in slot arg16; fall through (epsilon).
      assert_position,   //!< Epsilon; proceeds only if assertion arg8 holds here.
      match,             //!< Accept.
      assert_lookaround, //!< Epsilon; proceeds only if the lookaround sub-program arg16 holds here.
    };

    /*!
     * \brief Kind of zero-width assertion carried in `assert_position`'s arg8.
     *
     * Multiline and trailing-newline subtleties are resolved at compile time; the
     * engine only evaluates these predicates at a position.
     */
    enum class assert_kind : std::uint8_t
    {
      text_start,                //!< `\A`, and `^` without multiline.
      text_end,                  //!< `\Z`.
      text_end_or_final_newline, //!< `$` without multiline (Python semantics).
      line_start,                //!< `^` with multiline.
      line_end,                  //!< `$` with multiline.
      word_boundary,             //!< `\b` (Unicode word-ness in text mode; ASCII in bytes / `re.A`).
      not_word_boundary,         //!< `\B`.
      word_start,                //!< `\<` (non-word/start on the left, word on the right).
      word_end,                  //!< `\>` (word on the left, non-word/end on the right).
    };

    /*!
     * \brief Direction of a lookaround sub-pattern.
     */
    enum class look_dir : std::uint8_t
    {
      ahead,  //!< `(?=` / `(?!` — the sub matches starting at the position.
      behind, //!< `(?<=` / `(?<!` — the sub matches ending exactly at the position.
    };

    /*!
     * \brief A bounded lookaround sub-program, referenced by `assert_lookaround`'s arg16.
     *
     * The sub-pattern's bytecode lives as a region inside the main `code` buffer (appended
     * after the main program), so it survives copy/move of \ref dynamic_program with no
     * stored pointers — the views are rebuilt on demand from these offsets. `l_max` bounds
     * the bytes the sub can consume (unbounded sub-patterns are rejected at compile time):
     * the source of the strict linear-time guarantee.
     */
    struct lookaround_sub
    {
      std::int32_t code_offset {};                //!< First instruction of the sub-program in `code`.
      std::int32_t code_length {};                //!< Instruction count of the sub-program.
      std::int32_t l_max       {};                //!< Max bytes the sub-pattern can consume (bounded).
      look_dir     direction   {look_dir::ahead}; //!< Ahead or behind.
      bool         negative    {};                //!< `(?!` / `(?<!` (negated assertion).
    };

    /*!
     * \brief One NFA instruction. Field meaning depends on \ref op.
     */
    struct instr
    {
      opcode        op;                  //!< The operation.
      std::uint8_t  arg8             {}; //!< Byte literal, or \ref assert_kind, depending on op.
      std::uint16_t arg16            {}; //!< Class index (klass) or capture slot (save).
      std::int32_t  primary_target   {}; //!< Primary branch target (split/jump).
      std::int32_t  secondary_target {}; //!< Secondary branch target (split).
    };

    /*!
     * \brief Search-acceleration hints extracted from a compiled program.
     *
     * Filled by `analyze_program` (prefilter.hpp). The engine consults them to
     * skip positions that cannot start a match and to take fast paths; they never
     * change \e what matches, only how fast.
     */
    struct pattern_hints
    {
      std::array<char, 16> prefix                {};   //!< Required literal prefix (possibly truncated).
      std::uint8_t         prefix_size           {};   //!< Valid bytes in \ref prefix.
      bool                 anchored_start        {};   //!< `\A` / `^` (no multiline): only position 0.
      bool                 line_anchored         {};   //!< `^` multiline: position 0 or after `\n`.
      bool                 first_bytes_valid     {};   //!< False when an empty match is possible.
      bool                 empty_match_possible  {};   //!< The pattern can match the empty string (the nullable gate; conservative: assertions/lookarounds pass through, so e.g. `^$` is flagged nullable).
      std::int16_t         single_first          {-1}; //!< The unique possible first byte, or -1.
      char_class           first_bytes;                //!< All possible first bytes.
      std::array<char, 4>  small_set             {};   //!< The 2..4 possible first bytes, enumerated (the memchr-cascade set). Empty unless \ref small_set_size is set.
      std::uint8_t         small_set_size        {};   //!< Number of valid members in \ref small_set — 0 when not a small set, else 2..4. Drives the memchr-cascade scan in place of the bitmap loop.
      std::int32_t         greedy_class_loop     {-1}; //!< Class index if the whole pattern is "class+", else -1.
      std::int32_t         greedy_cp_class       {-1}; //!< cp_class index if the whole pattern is a code-point class `klass_cp` (optionally `+`), else -1.
      bool                 greedy_cp_class_plus  {};   //!< The \ref greedy_cp_class pattern is a greedy `+` loop (vs a single code point).
      std::int16_t         greedy_group_start    {-1}; //!< For a class-loop wrapped in one capturing group (`(\w+)`, `([a-z]+)`): the group's start slot to mirror the whole-match start into (-1 = no enveloping group).
      std::int16_t         greedy_group_end      {-1}; //!< The enveloping group's end slot (mirrors the whole-match end).
      bool                 fixed_shape           {};   //!< Whole pattern is a fixed-width byte/klass sequence (no branches/asserts/captures).
      std::int32_t         codepoint_class_ascii {-1}; //!< ASCII-class index when the whole pattern is `.`/negated-class (optionally `+`), else -1.
      bool                 codepoint_class_plus  {};   //!< The \ref codepoint_class_ascii pattern is a greedy `+` loop (vs a single codepoint).
      bool                 fixed_alternation     {};   //!< Whole pattern is an alternation of straight-line branches (no captures/asserts).

      /*!
       * \brief Length of the pure-literal match, or 0.
       *
       * Non-zero when the whole pattern is a fixed literal (the prefix bytes are
       * the entire match content, possibly with internal group saves but no
       * branches or further consuming ops). Enables a direct slot-replay bypass
       * of the full Pike VM — the major win for "search for a fixed string".
       */
      std::uint8_t exact_literal_len {};

      //! \brief For a whole-pattern `class+` run whose accepted set has a small (<= 6-byte) complement:
      //!        the STOP bytes (the complement), driving the memchr-cascade run scan (OPT-C). Empty
      //!        unless \ref stop_set_size is set. Placed last so it never shifts the hot fields' offsets.
      std::array<char, 6> stop_set      {};
      std::uint8_t        stop_set_size {}; //!< Members in \ref stop_set — 0 when the complement is too large, else 1..6.

      //! \brief A *required* literal byte at a fixed offset from the match start that is statically rarer
      //!        than the pattern's first-byte set (OPT: the date `-` at offset 4). The search jumps by
      //!        `memchr`-ing this one byte (SIMD) and back-verifies from `found - rare_offset`, instead of
      //!        the per-byte first-byte bitmap loop on a common class. -1 when no such byte was found.
      std::int16_t rare_byte   {-1};
      std::uint8_t rare_offset {}; //!< The fixed byte offset of \ref rare_byte from the match start.

      //! \brief A *required inner literal* every match must contain (the memmem candidate the inner-literal
      //!        prefilter scans for), and how many top-level children precede it — the prefix the prefilter
      //!        reverse-matches from a candidate back to the match start. `inner_literal_len == 0` = none;
      //!        `inner_literal_prefix == 0` = the literal is at the head (reverse is the identity), `-1` = it
      //!        is nested with no clean prefix boundary. Filled at compile from the AST (raw bytes, so \ref
      //!        pattern_hints — a core type — need not know the frontend literal type). Not yet routed on.
      std::array<std::uint8_t, 16> inner_literal        {};
      std::uint8_t                 inner_literal_len    {};
      std::int32_t                 inner_literal_prefix {-1};
    };

    /*!
     * \brief A named capture group.
     *
     * The name is stored as a byte range into the pattern text rather than an
     * owned string, keeping the type constexpr-friendly.
     */
    struct named_group
    {
      std::int32_t group {}; //!< Capture group number.
      std::int32_t begin {}; //!< Start offset of the name in the pattern text.
      std::int32_t end   {}; //!< End offset (exclusive) of the name.
    };

    /*!
     * \brief Non-owning view of a compiled program — what the engine executes.
     *
     * The spans point into storage that must outlive the view (the owning regex
     * object). Both the dynamic and static storage policies expose one of these.
     */
    //! \brief The per-regex immutable lazy-DFA/one-pass cache (defined in onepass.hpp; a forward declaration
    //!        keeps this low-level header independent of it). A dynamic storage owns one and points its view
    //!        at it, so the byte-program and one-pass table are built once per regex, not per find_iter.
    struct regex_immutables;
    struct program_view
    {
      std::span<const instr>          code;                   //!< The instruction stream (main + lookaround regions).
      std::span<const char_class>     classes;                //!< Interned character classes.
      std::span<const named_group>    names;                  //!< Named capture groups.
      std::span<const lookaround_sub> lookarounds;            //!< Bounded lookaround sub-programs (regions of \ref code).
      std::span<const cp_class>       cp_classes;             //!< Match-time code-point classes (for `klass_cp`).
      std::span<const code_range>     cp_ranges;              //!< Flat range buffer the `cp_class` slices index into.
      std::span<const instr>          prefix_code;            //!< IL: inner-literal prefix sub-program (the reverse start-finder). Empty unless there is a required literal with a top-level prefix. Dynamic-only.
      std::span<const char_class>     prefix_classes;         //!< IL: classes for \ref prefix_code.
      std::span<const cp_class>       prefix_cp_classes;      //!< IL: code-point classes for \ref prefix_code.
      std::span<const code_range>     prefix_cp_ranges;       //!< IL: flat range buffer for \ref prefix_cp_classes.
      std::uint16_t                   slot_count   {2};       //!< `2 * (capture groups + 1)`.
      bool                            byte_mode    {};        //!< \ref flags::bytes mode — positions are raw bytes.
      bool                            unicode_word {};        //!< `\b \B \< \>` use Unicode word-ness (text mode, not bytes / `re.A`).
      pattern_hints                   hints;                  //!< Search-acceleration hints.
      regex_immutables*               immut        {nullptr}; //!< Per-regex DFA/one-pass cache (dynamic storage only; else null).
    };

    /*!
     * \brief Owning, heap-allocated program: the storage backing `real::regex`.
     */
    struct dynamic_program
    {
      std::vector<instr>          code;              //!< The instruction stream (main program + lookaround sub-program regions).
      std::vector<char_class>     classes;           //!< Interned character classes.
      std::vector<named_group>    names;             //!< Named capture groups.
      std::vector<lookaround_sub> lookarounds;       //!< Bounded lookaround sub-programs (regions of \ref code).
      std::vector<cp_class>       cp_classes;        //!< Match-time code-point classes (for `klass_cp`).
      std::vector<code_range>     cp_ranges;         //!< Flat range buffer the `cp_class` slices index into.
      std::vector<instr>          prefix_code;       //!< IL: the inner-literal prefix sub-program (the part before the literal), for the reverse start-finder. Empty unless there is a required literal with a top-level prefix. Dynamic-only (not built during constant evaluation).
      std::vector<char_class>     prefix_classes;    //!< IL: classes for \ref prefix_code.
      std::vector<cp_class>       prefix_cp_classes; //!< IL: code-point classes (klass_cp) for \ref prefix_code.
      std::vector<code_range>     prefix_cp_ranges;  //!< IL: flat range buffer for \ref prefix_cp_classes.
      std::uint16_t               slot_count   {2};  //!< `2 * (capture groups + 1)`.
      bool                        byte_mode    {};   //!< \ref flags::bytes mode.
      bool                        unicode_word {};   //!< `\b \B \< \>` use Unicode word-ness (text mode).
      pattern_hints               hints;             //!< Search-acceleration hints.

      // Codepoint-class marker, set by `emit_codepoint_class` at emission so the
      // prefilter need not reverse-engineer the emitted block's bytecode shape.
      std::int32_t codepoint_mark_ascii  {-1}; //!< ASCII sub-class index of an emitted codepoint-class block (-1 = none).
      std::int32_t codepoint_mark_offset {-1}; //!< Where that block starts (program offset); the whole-pattern hint requires offset 1.

      /*!
       * \brief Returns a non-owning \ref program_view over this program.
       */
      [[nodiscard]] constexpr program_view view() const
      {
        return {.code              = std::span<const instr>(code),
                .classes           = std::span<const char_class>(classes),
                .names             = std::span<const named_group>(names),
                .lookarounds       = std::span<const lookaround_sub>(lookarounds),
                .cp_classes        = std::span<const cp_class>(cp_classes),
                .cp_ranges         = std::span<const code_range>(cp_ranges),
                .prefix_code       = std::span<const instr>(prefix_code),
                .prefix_classes    = std::span<const char_class>(prefix_classes),
                .prefix_cp_classes = std::span<const cp_class>(prefix_cp_classes),
                .prefix_cp_ranges  = std::span<const code_range>(prefix_cp_ranges),
                .slot_count        = slot_count,
                .byte_mode         = byte_mode,
                .unicode_word      = unicode_word,
                .hints             = hints};
      }
    };
  } // namespace detail
} // namespace real

#endif // REAL_PROGRAM_HPP
