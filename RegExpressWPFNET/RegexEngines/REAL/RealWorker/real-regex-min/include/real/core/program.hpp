/*!
 * \file program.hpp
 * \brief Compiled form of a pattern and the public flags / error types.
 *
 * The NFA instruction set, the heap-allocated program the compiler produces, the non-owning view the
 * engine runs over, the compilation \ref real::flags, and \ref real::regex_error.
 */
#ifndef REAL_PROGRAM_HPP
#define REAL_PROGRAM_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

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

  /*! \brief Sentinel for "no position" / unset capture slot (akin to std::string::npos). */
  inline constexpr std::size_t npos {std::numeric_limits<std::size_t>::max()};

  /*!
   * \brief Compilation flags, mirroring Python's `re.I`, `re.M` and `re.S`.
   *
   * Combinable with \ref operator|. \ref flags::icase folds Unicode in text mode, ASCII only under
   * \ref flags::ascii.
   */
  enum class flags : std::uint16_t
  {
    none      = 0,        //!< No flags.
    icase     = 1,        //!< Case-insensitive (Unicode fold in text mode; ASCII under \ref flags::ascii).
    multiline = 2,        //!< `^` and `$` also match at line boundaries.
    dotall    = 4,        //!< `.` also matches `\n`.
    bytes     = 8,        //!< Binary mode: `.` and `[^…]` match raw bytes, not codepoints.
    verbose   = 16,       //!< Verbose mode (`re.X`): ignore unescaped whitespace and `#` comments outside classes.
    ecma = 32,            //!< ECMAScript: `$` (no multiline) matches only at the very end, not before a final `\n`; `.` (no dotall) also excludes `\r`.
    ascii = 64,           //!< ASCII mode (`re.A`): `\d \w \s \b` and icase stay ASCII, even in text mode. `.`, explicit classes and UTF-8 literals stay code-point-aware.
    dollar_endonly = 128, //!< `$` (no multiline) matches only at the very end, never before a final `\n` (Rust `\z`). Unlike \ref flags::ecma, `.` keeps the Python default.
    allow_raw_byte = 256, //!< Permits `\C` (RE2's raw byte) outside \ref flags::bytes, which always allows it. For byte-offset consumers (`real::compat::re2`): a `\C` span can end mid-code-point.
    ungreedy = 512,       //!< RE2 `(?U)`: a bare quantifier is lazy and a `?` suffix makes it greedy. Resolved at parse time; scoped like the inline flags (`(?U:…)`, `(?-U:…)`).
  };

  /*!
   * \brief Which match a search returns among those starting at the leftmost position (default `first`).
   */
  enum class match_semantics : std::uint8_t
  {
    first   = 0, //!< Leftmost-first (Perl / Python `re`): source-order priority decides. Default.
    longest = 1, //!< Leftmost-longest (POSIX / RE2 `set_longest_match`): the longest match wins.
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
    return static_cast<flags>(static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
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
    return static_cast<flags>(static_cast<std::uint16_t>(lhs) & static_cast<std::uint16_t>(rhs));
  }

  /*!
   * \brief \p value with every flag in \p removed cleared -- the `(?flags-flags)` removal.
   *
   * No `operator~` on purpose: a complement sets every unassigned bit, naming flags that do not exist.
   * The complement is taken in `unsigned` and narrowed after; a `std::uint8_t` intermediate would drop
   * \ref flags::ungreedy (512).
   *
   * \param[in] value   The flag set to clear from.
   * \param[in] removed The flags to clear.
   * \return \p value without \p removed.
   */
  constexpr flags flags_without(flags value,
                                flags removed)
  {
    return static_cast<flags>(
      static_cast<std::uint16_t>(static_cast<unsigned>(value) & ~static_cast<unsigned>(removed)));
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
   * \brief Whether a rejected pattern is malformed (`syntax`) or well formed but beyond REAL's linear
   *        engine (`unsupported`).
   *
   * `unsupported` covers a backreference, a conditional and an unbounded lookaround, which a linear-time
   * engine cannot represent. Stable and exposed by the C ABI, so a binding never parses
   * \ref regex_error::what.
   *
   * `real::regex` has no escape hatch. To run such a pattern anyway, construct a `real::compat::regex`
   * (`real/compat/std/%regex.hpp`) with `real::compat::policy::fallback`: it delegates that pattern to
   * `std::regex` and forfeits the linear-time guarantee for it only.
   */
  // Keep the `%` above: unescaped, the path autolinks to a file page the site does not publish.
  enum class error_kind : std::uint8_t
  {
    syntax,
    unsupported,
  };

  /*!
   * \brief The exception every rejected pattern throws: a message, the pattern offset, and an
   *        \ref error_kind to branch on. In a constexpr context (`static_regex`) the throw is a
   *        compile-time error whose trace carries the message.
   */
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
        cause_(message),
        position_(position),
        kind_(kind)
    {}

    /*!
     * \brief Whether the pattern is malformed (`syntax`) or well-formed but unsupported by REAL.
     * \return The classification.
     */
    [[nodiscard]] error_kind kind() const noexcept
    {
      return kind_;
    }

    /*!
     * \brief Returns the formatted error message (with position).
     * \return The message, valid for this object's lifetime.
     */
    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

    /*!
     * \brief Returns the byte offset in the pattern where the error was found.
     * \return The offset into the pattern text.
     */
    [[nodiscard]] std::size_t position() const noexcept
    {
      return position_;
    }

    /*!
     * \brief Returns the cause WITHOUT the `regex_error at N: ` prefix that \ref what adds.
     *
     * For a consumer that reports the position itself (a Python `error.pos`, a rethrow adding
     * context), so none re-parses the formatted message.
     *
     * \return The unprefixed message, valid for this object's lifetime.
     */
    [[nodiscard]] const std::string& cause() const noexcept
    {
      return cause_;
    }

  private:

    std::string message_;  //!< Formatted message returned by what().
    std::string cause_;    //!< The same message without the position prefix (see cause()).
    std::size_t position_; //!< Offset in the pattern text.
    error_kind  kind_;     //!< Malformed (syntax) vs unsupported-by-REAL.
  };

  namespace detail {

    /*!
     * \brief An inclusive code-point range `[lo, hi]`, shared by ast.hpp's classes and the generated
     *        Unicode tables; here so those headers need not include the parser.
     */
    struct code_range
    {
      std::uint32_t lo {}; //!< First code point (inclusive).
      std::uint32_t hi {}; //!< Last code point (inclusive).
    };

    /*!
     * \brief A match-time code-point class for `klass_cp`: an ASCII bitmap below 0x80 plus a slice of
     *        sorted non-ASCII ranges in the program's flat `cp_ranges`, negation already applied. Unlike
     *        the byte-NFA `klass`, the ranges are kept and searched at match time (O(log ranges)).
     */
    struct cp_class
    {
      char_class    ascii;             //!< Members `< 0x80`.
      std::uint32_t range_begin {};    //!< First range in the program's `cp_ranges` buffer.
      std::uint32_t range_count {};    //!< Number of ranges belonging to this class.
      //! Content identity (FNV-1a of bitmap and ranges), set once at intern and never re-hashed per probe.
      //! The thread-local `cp_hi` cache keys by it, not by pointer: a recycled `cp_ranges` address must not hit.
      std::uint64_t fingerprint {};
    };

    //! \brief FNV-1a 64-bit offset basis. Reference it, never re-type it: a wrong basis still hashes,
    //!        just not as FNV-1a, and nothing downstream notices.
    inline constexpr std::uint64_t fnv1a_offset_basis {14695981039346656037ULL};

    inline constexpr std::uint64_t fnv1a_prime        {1099511628211ULL}; //!< FNV-1a 64-bit prime, paired with \ref fnv1a_offset_basis.

    /*!
     * \brief FNV-1a 64-bit content fingerprint of an ASCII bitmap and a range span, computed once at
     *        `intern_cp_class` (constexpr, for `static_regex`); match time reads \ref cp_class::fingerprint.
     * \param[in] ascii       The class's ASCII bitmap.
     * \param[in] ranges      Pointer to its first non-ASCII range.
     * \param[in] range_count Ranges belonging to it.
     * \return The content hash, equal for two classes holding the same code points.
     */
    [[nodiscard]] constexpr std::uint64_t fingerprint_cp_class_content(
      const char_class&                     ascii,
      const code_range*                     ranges,
      std::uint32_t                         range_count)
    {
      std::uint64_t h {fnv1a_offset_basis};
      const auto    mix {[&h](std::uint64_t v) constexpr {
                           h ^= v;
                           h *= fnv1a_prime;
                         }};
      for (const std::uint64_t word : ascii.bits) {
        mix(word);
      }
      mix(range_count);
      for (std::uint32_t k {0}; k < range_count; ++k) {
        mix(ranges[k].lo);
        mix(ranges[k].hi);
      }
      return h;
    }

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
      // Possessive tail: the optional part of a possessive quantifier over one bare atom (mandatory copies
      // are plain atoms ahead). On a match, consume and fall through; looping is a `jump` back or unrolled
      // copies (compiler.hpp's emit_tier1_loop). `primary_target` is a capture start slot (-1 none), written
      // with the consume on success only. On no match, epsilon in place to `secondary_target`: sound only
      // because it never crosses a position, hence a bare or singly-captured atom.
      byte_loop_possessive,     //!< Consume one byte == arg8. See the note above.
      klass_loop_possessive,    //!< Consume one byte in classes[arg16]. See the note above.
      klass_cp_loop_possessive, //!< Consume one code point in cp_classes[arg16], followed by the same 3-slot continuation chain as klass_cp. See the note above.
    };

    /*!
     * \brief Kind of zero-width assertion carried in `assert_position`'s arg8. Multiline and
     *        trailing-newline variants are resolved at compile time.
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
      line_start_cr,             //!< `^` with multiline under ecma: the text start, or after `\n` or `\r`.
      line_end_cr,               //!< `$` with multiline under ecma: the text end, or before `\n` or `\r`.
    };

    /*! \brief Direction of a lookaround sub-pattern. */
    enum class look_dir : std::uint8_t
    {
      ahead,  //!< `(?=` / `(?!` — the sub matches starting at the position.
      behind, //!< `(?<=` / `(?<!` — the sub matches ending exactly at the position.
    };

    /*!
     * \brief A bounded lookaround sub-program, referenced by `assert_lookaround`'s arg16.
     *
     * Its code is a region of the main `code` buffer, addressed by offset so a copy or move of
     * \ref dynamic_program stores no pointer. `l_max` bounds what the sub consumes (unbounded ones are
     * rejected at compile time), which keeps the search linear.
     */
    struct lookaround_sub
    {
      std::int32_t code_offset {};                //!< First instruction of the sub-program in `code`.
      std::int32_t code_length {};                //!< Instruction count of the sub-program.
      std::int32_t l_max       {};                //!< Max bytes the sub-pattern can consume (bounded).
      look_dir     direction   {look_dir::ahead}; //!< Ahead or behind.
      bool         negative    {};                //!< `(?!` / `(?<!` (negated assertion).
    };


    /*! \brief One NFA instruction. Field meaning depends on \ref op. */
    struct instr
    {
      opcode        op;                  //!< The operation.
      std::uint8_t  arg8             {}; //!< Byte literal, or \ref assert_kind, depending on op.
      std::uint16_t arg16            {}; //!< Class or cp_class index, capture slot (save), or lookaround index.
      std::int32_t  primary_target   {}; //!< Primary branch target (split/jump); capture start slot for the possessive opcodes.
      std::int32_t  secondary_target {}; //!< Secondary branch target (split; possessive opcodes on no match).
    };

    /*!
     * \brief Which operand space a `class_ref` indexes: the literal byte (`byte_loop_possessive`),
     *        `classes[]` (`klass_loop_possessive`) or `cp_classes[]` (`klass_cp_loop_possessive`).
     *        `none` = unarmed.
     */
    enum class class_kind : std::uint8_t
    {
      none,
      byte,
      klass,
      klass_cp,
    };

    /*!
     * \brief A typed reference into a possessive-loop body's operand space.
     *
     * Typed so equality compares \ref kind before the index: with bare indices, index 0 of two
     * different tables compared equal and `[abc].*+` matched unconditionally.
     */
    struct class_ref
    {
      class_kind    kind  {class_kind::none}; //!< Which table \ref index refers to; `none` means unarmed.
      std::uint16_t index {};                 //!< classes[]/cp_classes[] index (kind == klass/klass_cp), or the literal byte value 0-255 (kind == byte).

      /*!
       * \brief Whether this reference names anything at all.
       * \return False while \ref kind is \c class_kind::none.
       */
      [[nodiscard]] constexpr bool armed() const noexcept
      {
        return kind != class_kind::none;
      }

      /*! \brief Equality, comparing \ref kind first so two different tables' indices never collide. */
      friend constexpr bool operator==(const class_ref&,
                                       const class_ref&) noexcept = default;
    };

    /*! \brief Capture slots the bounded backtracker carries in a fixed array (16 groups and group 0). */
    inline constexpr std::size_t bounded_backtrack_max_slots {34};

    /*!
     * \brief The bounded backtracker's budget: one bit per (instruction, position), (n + 1) x m bits for
     *        an n-byte subject and an m-instruction program.
     *
     * The bitmap is on the stack: a kibibyte, beside two for the pending branches held before spilling
     * to the heap.
     */
    inline constexpr std::size_t bounded_backtrack_bits {8192};

    /*!
     * \brief Search-acceleration hints extracted from a compiled program by `analyze_program`
     *        (prefilter.hpp). They change how fast, never \e what matches.
     *
     * \note **The field order is load-bearing.** Offsets 0..86 are the hot prefix every search reads
     *       (\ref prefix, \ref first_bytes, the `greedy_*` selectors, \ref fixed_shape,
     *       \ref fixed_alternation); the tail is per-route. **Append new fields at the END**: an insertion
     *       reflows every later field and has moved hot fields across a cache line, measurably on patterns
     *       that read none of it. Do not split into hot and cold structs: an indirection to the tail would
     *       charge every route that reads it.
     */
    struct pattern_hints
    {
      std::array<char, 16> prefix                   {};   //!< Required literal prefix (possibly truncated).
      std::uint8_t         prefix_size              {};   //!< Valid bytes in \ref prefix.
      bool                 anchored_start           {};   //!< `\A` / `^` (no multiline): only position 0.
      std::uint8_t         line_anchored            {};   //!< `^` multiline: 0 none, 1 position 0 or after `\n`, 2 also after `\r` (ecma).
      bool                 first_bytes_valid        {};   //!< False when an empty match is possible.
      bool                 empty_match_possible     {};   //!< The pattern can match the empty string. Conservative: assertions count as nullable, so `^$` is flagged.
      bool                 nullable_captured_repeat {};   //!< A capturing group with a nullable body sits under a quantifier (`(ab|)+a`); set from the AST (compiler.hpp). REAL captures the last consuming iteration, an ECMAScript backtracker an extra empty one, so `real::compat` sends replace/iterate to std (\ref real::compat::basic_regex::uses_real_traversal). Over-approximating is safe.
      std::int16_t         single_first             {-1}; //!< The unique possible first byte, or -1.
      char_class           first_bytes;                   //!< All possible first bytes.
      std::int32_t         greedy_class_loop        {-1}; //!< Class index if the whole pattern is "class+", else -1.
      //! \brief Trailing end anchor on \ref greedy_class_loop — 0 none, 1 `\Z`, 2 `$` (also before one final
      //!        newline). The route walks the run BACKWARD from that limit instead of scanning and retrying.
      std::uint8_t         greedy_class_loop_end     {};
      std::uint16_t        greedy_class_loop_min     {1};  //!< Minimum run length (bytes) for \ref greedy_class_loop — 1 for `X+`, k for `X{k,}`.
      std::int32_t         greedy_cp_class           {-1}; //!< cp_class index if the whole pattern is a code-point class `klass_cp` (optionally `+`), else -1.
      bool                 greedy_cp_class_plus      {};   //!< The \ref greedy_cp_class pattern is a greedy `+` loop (vs a single code point).
      //! \brief Trailing end anchor on \ref greedy_cp_class, encoded as
      //!        \ref greedy_class_loop_end.
      std::uint8_t         greedy_cp_class_end       {};
      std::uint16_t        greedy_cp_class_min       {1};  //!< Minimum run length in CODE POINTS for \ref greedy_cp_class (with \ref greedy_cp_class_plus).
      std::int16_t         greedy_group_start        {-1}; //!< For a class loop wrapped in one capturing group (`(\w+)`): the group's start slot, mirroring the match start (-1 = none).
      std::int16_t         greedy_group_end          {-1}; //!< The enveloping group's end slot (mirrors the whole-match end).
      bool                 fixed_shape               {};   //!< Whole pattern is a fixed-width byte/klass sequence (no branches/asserts/captures).
      std::int32_t         codepoint_class_ascii     {-1}; //!< ASCII-class index when the whole pattern is `.`/negated-class (optionally `+`), else -1.
      bool                 codepoint_class_plus      {};   //!< The \ref codepoint_class_ascii pattern is a greedy `+` loop (vs a single codepoint).
      bool                 fixed_alternation         {};   //!< Whole pattern is an alternation of straight-line branches (no captures/asserts).

      /*!
       * \brief Length of the pure-literal match, or 0.
       *
       * Non-zero when the prefix bytes are the whole match (group saves allowed, no branch or other
       * consuming op), so a match is replayed without the Pike VM.
       */
      std::uint8_t exact_literal_len {};

      //! \brief For a whole-pattern `class+` whose accepted set has a complement of <= 6 bytes: those STOP
      //!        bytes, driving the run scan. Kept out of the hot prefix (see the layout note).
      std::array<char, 6> stop_set      {};
      std::uint8_t        stop_set_size {}; //!< Members in \ref stop_set — 0 when the complement is too large, else 1..6.

      //! \brief The 2..8 possible first bytes, for the alternation route's masked block scan. In the cold
      //!        tail: next to \ref first_bytes it moved the `greedy_class_loop*` fields across a cache line.
      std::array<char, 8> small_set      {};
      std::uint8_t        small_set_size {}; //!< Members in \ref small_set — 0 when not a small set, else 2..8.

      //! \brief A required byte at a fixed offset from the match start, rarer than the first-byte set (the
      //!        date `-` at offset 4): the search `memchr`s it and back-verifies from `found - rare_offset`.
      //!        -1 when none.
      std::int16_t rare_byte   {-1};
      std::uint8_t rare_offset {}; //!< The fixed byte offset of \ref rare_byte from the match start.

      //! \brief Rare discriminant with an optional byte before it (URL `https?://…`): `memchr(rare_disc)`,
      //!        then back-verify `[prefix][opt?][disc][after]`. Chosen over a weak literal prefix (`http`)
      //!        when rarer. Unlike \ref rare_byte its offset is not fixed when \ref rare_disc_opt is set
      //!        (`s?` puts the colon at 4 or 5). -1 = unarmed.
      std::int16_t        rare_disc            {-1}; //!< Discriminant byte (e.g. `:`).
      std::array<char, 8> rare_disc_prefix     {};   //!< Required bytes before the optional (e.g. `http`).
      std::uint8_t        rare_disc_prefix_len {};   //!< Length of \ref rare_disc_prefix.
      std::int16_t        rare_disc_opt        {-1}; //!< Optional mono-byte before the disc (`s`), or -1.
      std::array<char, 4> rare_disc_after      {};   //!< Fixed bytes after the disc (e.g. `//`).
      std::uint8_t        rare_disc_after_len  {};   //!< Length of \ref rare_disc_after.

      //! \brief A required inner literal every match contains (the inner-literal prefilter's candidate), and
      //!        how many top-level children precede it (the prefix reverse-matched back to the match start).
      //!        Raw bytes, so this core type need not know the frontend literal type.
      std::array<std::uint8_t, 16> inner_literal        {};   //!< The literal's bytes, the first \ref inner_literal_len of which are meaningful.
      std::uint8_t                 inner_literal_len    {};   //!< Bytes held in \ref inner_literal; 0 means the pattern has no required inner literal.
      std::int32_t                 inner_literal_prefix {-1}; //!< Top-level children before the literal; 0 = at the head, -1 = nested with no clean boundary.

      /*!
       * \brief Every `save` in this program writes slot 0 or slot 1, so a thread's whole capture state is
       *        the group-0 START.
       *
       * Derived by scanning the code, so it holds for any producer. The epsilon walk then carries no
       * capture block: with `save 0` at pc 0, every thread one `add_thread` call adds shares one start. A
       * `save 0` behind a split would hand one branch its sibling's start, a wrong answer.
       */
      bool                         capture_free_walk    {false};

      //! \brief For a HOMOGENEOUS \ref fixed_shape (every position accepts the same set of <= 2 ranges:
      //!        `[0-9a-f]{8}`, `\d{4}`): the ranges and run length for the SIMD scan+verify in `run_fixed_shape`.
      std::uint8_t fixed_shape_lo0      {};  //!< First range's low byte.
      std::uint8_t fixed_shape_hi0      {};  //!< First range's high byte.
      std::uint8_t fixed_shape_lo1      {1}; //!< lo1 > hi1 (default 1 > 0) encodes "no second range".
      std::uint8_t fixed_shape_hi1      {};  //!< Second range's high byte, when one is present.
      std::uint8_t fixed_shape_simd_len {};  //!< The run length (1..16) when eligible, else 0.

      //! \brief A `fixed_shape` whose trailing end anchor was peeled: 0 none, 1 `\Z`, 2 `$` (also before ONE
      //!        final newline, which is why `^X$` is not `fullmatch(X)`). Set only with \ref anchored_start —
      //!        one candidate position, so a failed end test owes no retry. `$` without `^` stays on the VM.
      std::uint8_t fs_end_anchor {};

      //! \brief Nonzero when the general loop may answer a small subject by bounded backtracking
      //!        (`pike_vm::run_bounded_backtrack`): no lookaround, at most \ref bounded_backtrack_max_slots
      //!        slots; the subject budget is checked per search. A hint, not a runtime test, so blanking the
      //!        hints (how differentials reach the plain Pike VM) also disables it.
      std::uint8_t bounded_backtrack {};

      //! \brief Trailing lookaround on a groupless greedy `class+` (`[a-z]+(?=[a-z])`): index into
      //!        lookarounds, -1 = not this shape. Leaves \ref greedy_class_loop at -1 on purpose: sharing
      //!        that selector makes every `class+` search branch on this shape, measured dearer.
      std::int16_t trailing_lookaround {-1};
      std::int32_t trailing_la_class   {-1}; //!< Class index for \ref trailing_lookaround body; −1 if unset.

      //! \brief Possessive fast path, UNBOUNDED loops only (`X*+`/`X++`) with min 0 or 1: a bounded count
      //!        (`(a){2,4}+b`) lacks the "every start in the run reaches the same body end" invariant that
      //!        makes a skip-based search linear. A non-empty \ref possessive_prefix also needs the loop class
      //!        to exclude the prefix's and suffix's leading bytes, or the delimited runner's retry is
      //!        quadratic (`id=[a-z0-9]*+;` stays on the general VM).
      std::array<char, 8>   possessive_prefix       {};   //!< Required literal BEFORE the loop (0 len = none; the delimited/"quoted" shape).
      std::uint8_t          possessive_prefix_size  {};   //!< Meaningful bytes of \ref possessive_prefix; 0 selects the bare/suffixed shape, non-zero the delimited one.
      std::array<char, 8>   possessive_suffix       {};   //!< Required literal AFTER the loop (0 len = none; e.g. the 'x' in `\d++x`).
      std::uint8_t          possessive_suffix_size  {};   //!< Meaningful bytes of \ref possessive_suffix; 0 means the loop has no required suffix.
      class_ref             possessive_class        {};   //!< The loop body's class, typed by opcode.
      std::int16_t          possessive_group_start  {-1}; //!< Enveloping single capture group's start slot (mirrors \ref greedy_group_start), -1 = none.
      std::int16_t          possessive_group_end    {-1}; //!< The enveloping group's end slot.
      bool                  possessive_min_nonzero  {};   //!< True for `X++` (one mandatory copy): a candidate MUST be in-class; false for `X*+`, where an empty body is a candidate anywhere.

      //! \brief Optional word-boundary wrap on fixed_shape / fixed_alternation / exact_literal, checked in
      //!        O(1) at the match start/end once the body route accepts a candidate.
      std::uint8_t wb_lead  {}; //!< Leading wrap: 0 none, 1 `\b`, 2 `\B` — asserted at the match start.
      std::uint8_t wb_trail {}; //!< Trailing wrap, same encoding as \ref wb_lead — asserted at the match end.
      //! \brief A leading `\b` was dropped by the DROP rule (\ref resolve_class_wb_hints): a maximal run
      //!        starts only after a non-word character, assuming "no preceding character" means the text
      //!        start. A caller's `search(text, pos)` with `pos > 0` breaks that (`pos` is no virtual start
      //!        for `\b`, as in Python; `endpos` is a virtual end, so only the lead side needs this): a
      //!        runner whose first candidate is `start` itself must check `assertion_holds` there.
      bool wb_lead_maximal_run {};
      //! \brief First consuming (byte/klass) or branch pc for fixed_shape / alternation after
      //!        save 0 and an optional lead `\b`/`\B`. Default 1 (no lead wrap).
      std::uint8_t body_pc {1};

      //! \brief Branch count for \ref fixed_alternation (0 when unset); the alternation route picks
      //!        Aho-Corasick past a threshold. Appended last: earlier, it reflows \ref wb_lead and
      //!        \ref body_pc (read per match), and after \ref small_set_size it grows the struct.
      std::uint16_t alternation_branch_count {};

      //! \brief Leading top-level children peeled before the IL reverse prefix (the `\b` of `\b\w+@\w+\b`):
      //!        \ref build_prefix_ast skips these, then takes \ref inner_literal_prefix children.
      std::uint8_t inner_literal_prefix_skip {};

      //! \brief One `find_prefix` answers the whole exact-literal search; `run_exact_literal`'s
      //!        per-match steps are redundant for this program.
      //!
      //! Set when ALL hold: \ref exact_literal_len >= 2 (one byte goes through `find_byte`);
      //! \ref prefix_size == \ref exact_literal_len (so `literal_at`'s re-compare is redundant); no
      //! `assert_position` in `code` (`\bdog`, `^dog` need a check per occurrence); none of
      //! \ref anchored_start, \ref line_anchored, \ref rare_disc armed (each takes an earlier branch in
      //! `next_candidate`). Program-only terms, folded once rather than evaluated per match; the caller
      //! adds `slot_count == 2`. Assertion-freeness is read from `code`, never inferred from
      //! \ref wb_lead or the anchors: an assert kind they do not represent would make that unsound.
      bool literal_one_search {};

      //! \brief HETEROGENEOUS fixed-shape pair filter (`(?i)cafe`, `\w\d\w\d`): two positions, each with
      //!        its own <= 2-range set, reject 16 candidate starts per vector compare.
      //!
      //! A prefilter: the per-position walk then confirms. The pair is the two most selective positions
      //! (ties toward the widest separation). Set only when \ref fixed_shape_simd_len is 0, the width is
      //! 2..16, and every position has 1..2 ranges. `lo1 > hi1` means "no second range".
      std::uint8_t fs_pair_width {};  //!< The shape's byte width (2..16); 0 when the pair filter is ineligible.
      std::uint8_t fs_pair_off_a {};  //!< Offset of the first probed position within the shape.
      std::uint8_t fs_pair_off_b {};  //!< Offset of the second probed position.
      std::uint8_t fs_pair_a_lo0 {};  //!< Position A, first range's low byte.
      std::uint8_t fs_pair_a_hi0 {};  //!< Position A, first range's high byte.
      std::uint8_t fs_pair_a_lo1 {1}; //!< Position A, second range's low byte; `lo1 > hi1` encodes "no second range".
      std::uint8_t fs_pair_a_hi1 {};  //!< Position A, second range's high byte.
      std::uint8_t fs_pair_b_lo0 {};  //!< Position B, first range's low byte.
      std::uint8_t fs_pair_b_hi0 {};  //!< Position B, first range's high byte.
      std::uint8_t fs_pair_b_lo1 {1}; //!< Position B, second range's low byte; same "no second range" convention.
      std::uint8_t fs_pair_b_hi1 {};  //!< Position B, second range's high byte.

      /*!
       * \brief IL reverse-by-class: the inner-literal prefix is one greedy class loop (`[a-z]+@…`), so a
       *        candidate literal at `h` starts its match where the class run ending at `h` starts.
       *
       * No sub-program, DFA or allocation, so `static_regex` (no immutables) runs it; `confirm_at` verifies
       * forward, so a rejected candidate costs an advance, never a wrong match. -1 otherwise (a fixed count,
       * `\d{4}-…`, has no `split` and stays general).
       */
      std::int32_t il_rev_class {-1};
      bool         il_rev_is_cp {}; //!< \ref il_rev_class indexes `cp_classes` (a `klass_cp` loop) rather than `classes`.

      /*!
       * \brief IL two-run confirm: the whole pattern is `class+ <literal> class+` (groups around either run
       *        transparent), so a candidate is confirmed without an engine, every capture slot being one of
       *        four positions. For `static_regex`, which would otherwise confirm on the general VM. -1 when
       *        the suffix is not one greedy class loop ending the pattern.
       */
      std::int32_t il_fwd_class {-1};
      bool         il_fwd_is_cp {}; //!< \ref il_fwd_class indexes `cp_classes` rather than `classes`.
      //! \brief The literal can occur inside the prefix run, so the greedy prefix leaves it at its LAST
      //!        occurrence before the suffix, not at the candidate: the span is the same, the groups are not.
      bool         il_fwd_last {};
      //! \brief Both runs are one class, so the last occurrence the prefix run reaches is the last one before
      //!        the suffix's end: the fill need not walk the prefix run past the literal.
      bool         il_fwd_run_to_end {};

      /*!
       * \brief IL fixed code-point shape: the whole pattern is a fixed sequence of code-point atoms and
       *        literal bytes (`\d{4}-\d{2}-\d{2}`), no loop. Not byte-fixed (a Unicode `\d` is multi-byte),
       *        but the code-point count is: step \ref il_cp_prefix_cps code points back from the literal,
       *        then one forward walk verifies and fills every capture.
       */
      bool         il_cp_shape_eligible {};
      std::uint8_t il_cp_prefix_cps     {}; //!< Code points before the literal in \ref il_cp_shape_eligible.

      /*!
       * \brief Upper bound, in code points, on \ref greedy_cp_class's run, or 0 for unbounded (`\w+`,
       *        `\w{8,}`). `\w{8}` sets 8: the run must stop from above (`\w{8}` over a nine-letter word
       *        matches eight).
       */
      std::uint16_t greedy_cp_class_max {};

      /*!
       * \brief Class index when the whole pattern is a bare single byte-class (`[a-z]`; exactly `save 0`,
       *        `klass`, `save 1`, `match`), else -1.
       *
       * Without it the form matched no batchable selector and paid a route entry per match. A flag on
       * \ref greedy_class_loop would make every `class+` site branch on it (as \ref trailing_lookaround).
       * A literal byte takes `exact_literal` instead.
       */
      std::int32_t single_class {-1};

      //! \brief Offset of the rarest byte of \ref prefix (by `byte_frequency`), the byte the literal search
      //!        scans first (`find_literal_adaptive`). Meaningful when \ref prefix_size >= 2.
      std::uint8_t prefix_rare {};

      //! \brief Offset of the rarest byte of \ref inner_literal, as \ref prefix_rare. Meaningful when
      //!        \ref inner_literal_len >= 2.
      std::uint8_t inner_literal_rare {};

      //! \brief Compiled with \ref flags::allow_raw_byte, so a lead opening on a UTF-8 continuation byte (RE2's
      //!        `\C`) keeps its routes and lazy DFA, matches starting inside a code point as RE2's do. In text
      //!        mode otherwise such a lead is left to the VM. Fits the trailing padding.
      bool raw_byte_starts {};
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
     * \brief The per-regex immutable lazy-DFA/one-pass cache (defined in onepass.hpp; a forward declaration
     *        keeps this low-level header independent of it). A dynamic storage owns one and points its view
     *        at it, so the byte-program and one-pass table are built once per regex, not per find_iter.
     */
    struct regex_immutables;

    /*!
     * \brief A non-owning view of everything the engine needs to run one compiled pattern: the borrowed
     *        spans, the slot count, the mode flags, the search hints, and the per-regex cache. Both storages
     *        hand one of these to the VM, which is why the engine is storage-agnostic. Valid only as long as
     *        the program it views is alive.
     */
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
      //! \brief Flat byte-class membership tables, `class_tables[i * 256 + b]`, or null when the storage
      //!        has none pre-built (dynamic, which fills them lazily through \ref immut instead).
      //!
      //! Here, not in the state type, so one state type serves many patterns; the compile-time storage
      //! points these at `static constexpr` arrays a constant-folding compiler still sees through.
      const std::uint8_t*             class_tables    {nullptr};
      const std::uint8_t*             cp_ascii_tables {nullptr}; //!< Flat ASCII tables per `cp_class`: `[i * 256 + b]`. Null as \ref class_tables.
      const std::uint64_t*            cp_page_tables  {nullptr}; //!< Flat page bitmaps per `cp_class`: `[i * 30]`. Null as \ref class_tables.
    };

    /*!
     * \brief The view is copied on every `find_iter` and `count_matches` call: a fixed per-call cost under
     *        any throughput row's noise floor, so its size is guarded by counting bytes, not by timing.
     *
     * Lower the ceiling when the view shrinks; raise it only with the reason written down. 64-bit only:
     * `std::span` is two pointers, and a 32-bit CI leg exists.
     */
    static_assert(sizeof(void*) != 8 || sizeof(program_view) <= 440,
                  "program_view grew: it is copied once per find_iter/count_matches call. See the note "
                  "above -- raise this ceiling deliberately, with the per-call cost accepted in writing.");

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
      // Codepoint-class marker set by `emit_any_codepoint_class`, so the prefilter need not
      // reverse-engineer the block, whose length varies with the UTF-8 range split.
      std::int32_t codepoint_mark_ascii  {-1};       //!< ASCII sub-class index of an emitted codepoint-class block (-1 = none).
      std::int32_t codepoint_mark_offset {-1};       //!< Where that block starts (program offset); the whole-pattern hint requires offset 1.
      std::int32_t codepoint_mark_end    {-1};       //!< Program offset right after that block ends (-1 = none).

      /*!
       * \brief Returns a non-owning \ref program_view over this program.
       * \return The view; valid as long as this program is alive and unmodified.
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
