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

#include "version.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "charclass.hpp"

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
    none      = 0,  //!< No flags.
    icase     = 1,  //!< Case-insensitive (ASCII).
    multiline = 2,  //!< `^` and `$` also match at line boundaries.
    dotall    = 4,  //!< `.` also matches `\n`.
    bytes     = 8,  //!< Binary mode: `.` and `[^…]` match raw bytes, not codepoints.
    verbose   = 16, //!< Verbose mode (`re.X`): ignore unescaped whitespace and `#` comments outside classes.
    ecma = 32,      //!< ECMAScript compatibility: `$` (no multiline) matches only at the very end (not before a final `\n`, the Python default), AND `.` (no dotall) also excludes `\r` (ECMAScript excludes `\n` and `\r`; the multi-byte U+2028/U+2029 have no byte-level effect).
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
  class regex_error : public std::exception
  {
  public:

    /*!
     * \brief Builds the error.
     * \param[in] message  Human-readable cause.
     * \param[in] position Byte offset in the pattern where the error was found.
     */
    regex_error(const std::string& message,
                std::size_t        position)
      : message_("regex_error at " + std::to_string(position) + ": " + message),
        position_(position)
    {}

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
  };

  namespace detail {

    /*!
     * \brief NFA instruction opcodes executed by the Pike VM.
     */
    enum class opcode : std::uint8_t
    {
      byte,              //!< Consume one byte equal to arg8; fall through to pc+1.
      klass,             //!< Consume one byte in classes[arg16]; fall through to pc+1.
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
      word_boundary,             //!< `\b` (ASCII word characters).
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
      std::int32_t         greedy_class_loop     {-1}; //!< Class index if the whole pattern is "class+", else -1.
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

      //! \brief True if the program contains a lookaround; forces the general VM (no DFA, no fast path).
      bool has_lookaround {};
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
    struct program_view
    {
      std::span<const instr>          code;           //!< The instruction stream (main + lookaround regions).
      std::span<const char_class>     classes;        //!< Interned character classes.
      std::span<const named_group>    names;          //!< Named capture groups.
      std::span<const lookaround_sub> lookarounds;    //!< Bounded lookaround sub-programs (regions of \ref code).
      std::uint16_t                   slot_count {2}; //!< `2 * (capture groups + 1)`.
      bool                            byte_mode  {};  //!< \ref flags::bytes mode — positions are raw bytes.
      pattern_hints                   hints;          //!< Search-acceleration hints.
    };

    /*!
     * \brief Owning, heap-allocated program: the storage backing `real::regex`.
     */
    struct dynamic_program
    {
      std::vector<instr>          code;           //!< The instruction stream (main program + lookaround sub-program regions).
      std::vector<char_class>     classes;        //!< Interned character classes.
      std::vector<named_group>    names;          //!< Named capture groups.
      std::vector<lookaround_sub> lookarounds;    //!< Bounded lookaround sub-programs (regions of \ref code).
      std::uint16_t               slot_count {2}; //!< `2 * (capture groups + 1)`.
      bool                        byte_mode  {};  //!< \ref flags::bytes mode.
      pattern_hints               hints;          //!< Search-acceleration hints.

      // Codepoint-class marker, set by `emit_codepoint_class` at emission so the
      // prefilter need not reverse-engineer the emitted block's bytecode shape.
      std::int32_t codepoint_mark_ascii  {-1}; //!< ASCII sub-class index of an emitted codepoint-class block (-1 = none).
      std::int32_t codepoint_mark_offset {-1}; //!< Where that block starts (program offset); the whole-pattern hint requires offset 1.

      /*!
       * \brief Returns a non-owning \ref program_view over this program.
       */
      [[nodiscard]] constexpr program_view view() const
      {
        return {.code        = std::span<const instr>(code),
                .classes     = std::span<const char_class>(classes),
                .names       = std::span<const named_group>(names),
                .lookarounds = std::span<const lookaround_sub>(lookarounds),
                .slot_count  = slot_count,
                .byte_mode   = byte_mode,
                .hints       = hints};
      }
    };
  } // namespace detail
} // namespace real

#endif // REAL_PROGRAM_HPP
