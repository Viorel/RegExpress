/*!
 * \file re2/re2.hpp
 * \brief `real::compat::re2` — an RE2-compatible drop-in, the `RE2` class surface.
 *
 * Names follow RE2's `re2.h` exactly (`FullMatch`, `set_longest_match`, `ANCHOR_START`, …). This wraps
 * `real::regex` and `real::regex_set`: no RE2 dependency at run time.
 *
 * REAL compiles everything RE2 compiles, plus bounded lookarounds, possessive quantifiers and wider Unicode
 * properties (`\p{sc=…}`, `\p{Emoji}`, …). It is deliberately stricter in two places: duplicate group names
 * (match-by-name would be ambiguous) and surrogate code points in `\x{…}`, `\u`, `\U`, `\N` (not Unicode
 * scalars). Both surface as `ok() == false`.
 *
 * **RE2's reading of the syntax.** `\w \d \s` are ASCII, `\b \B` read ASCII word characters, `[[:alpha:]]` is a
 * POSIX class, `$` without `(?m)` is the end of the text, and a class member folds before it is negated
 * (`(?i)\W` leaves out the Kelvin sign): see re2/flavor.hpp. `GlobalReplace` walks as RE2's does. Case folding
 * keeps REAL's tables, which are Python's: under `(?i)`, `İ` and `ı` (U+0130, U+0131) go with `i` and `I`,
 * where RE2 keeps them apart.
 *
 * **No fallback, no exceptions.** A pattern or option this layer cannot honor is rejected as RE2 rejects a
 * syntax error: `ok() == false`, with `error()` saying why.
 *
 * **Scope.** RE2's default mode: UTF-8, no `posix_syntax`. `posix_syntax`, Latin-1, `never_nl` and
 * `never_capture` are rejected at construction (`ErrorUnsupported`), never ignored; `perl_classes`,
 * `word_boundary` and `one_line` are inert, as in RE2 outside `posix_syntax`. `longest_match` is honored by
 * every unanchored search (`PartialMatch`, `FindAndConsume`, `Replace`, `GlobalReplace`); captures inside
 * an ambiguous alternation then carry RE2's own non-POSIX caveat.
 *
 * **`\C` is accepted**, as in RE2: exactly one byte, possibly mid-codepoint. Safe here because this API is
 * byte-offset, as RE2's is; REAL's char-offset surfaces keep it behind `flags::bytes`.
 */
#ifndef REAL_RE2_RE2_HPP
#define REAL_RE2_RE2_HPP

// A public entry point; the re2/arg.hpp it includes is internal.

#include <real/version.hpp>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <real/real.hpp>
#include <real/regex_set.hpp>

#include "arg.hpp"
#include "flavor.hpp"

namespace real::compat::re2 {

  /*!
   * \brief RE2-compatible drop-in for `RE2`. Backed by `real::regex` — linear-time, ReDoS-safe.
   *
   * Copyable and movable, where RE2 deletes both (`real::regex` copies cheaply). A rejected pattern does not
   * throw: `ok()`, `error()` and `error_code()` report it, as RE2 does.
   */
  class RE2
  {
  public:

    /*!
     * \brief RE2's canned option sets, convertible to \ref Options as RE2's are, so `RE2 re(p, RE2::Quiet)` reads as
     *        it does there. `Latin1` and `POSIX` select what this layer rejects at construction (see `Options`).
     */
    enum CannedOptions : std::uint8_t
    {
      DefaultOptions = 0, //!< The default options.
      Latin1,             //!< `EncodingLatin1`: rejected at construction.
      POSIX,              //!< `posix_syntax` and `longest_match`: rejected at construction.
      Quiet,              //!< `log_errors` off.
    };

    /*!
     * \brief RE2-compatible construction options. Mirrors real RE2's `RE2::Options` field-for-field
     *        (names, defaults); see the file-level doc comment for which fields this layer honors.
     */
    class Options
    {
    public:

      /*!
       * \brief Pattern/text encoding — `real::compat::re2` supports `EncodingUTF8` only.
       */
      enum Encoding : std::uint8_t
      {
        EncodingUTF8,  //!< UTF-8 (the default, and the only encoding this layer honors).
        EncodingLatin1 //!< Latin-1 — accepted for API shape, rejected at `RE2` construction.
      };

      /*!
       * \brief Default options: UTF-8, leftmost-first, case-sensitive, every RE2 default kept.
       */
      Options() = default;

      /*!
       * \brief One of RE2's canned option sets. Implicit, as RE2's own, so a canned set passes where `Options` is
       *        taken.
       * \param[in] canned The set.
       */
      Options(CannedOptions canned) // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
        : encoding_ {canned == Latin1 ? EncodingLatin1 : EncodingUTF8},
          posix_syntax_ {canned == POSIX},
          longest_match_ {canned == POSIX},
          log_errors_ {canned != Quiet}
      {}

      /*!
       * \brief The compiled-pattern budget, as RE2's: a program past two thirds of it fails with
       *        `ErrorPatternTooLarge` (at most 1, a 100 000-instruction bound applies instead). Counted in
       *        REAL's program bytes (12 per instruction against RE2's 8, no shared prefixes), so a lowered
       *        budget does not reject exactly what RE2 rejects; the default rejects nothing REAL compiles.
       * \return The budget, in bytes.
       */
      [[nodiscard]] std::int64_t max_mem() const noexcept
      {
        return max_mem_;
      }

      /*!
       * \brief Sets `max_mem`.
       * \param[in] value The new budget, in bytes.
       */
      void set_max_mem(std::int64_t value) noexcept
      {
        max_mem_ = value;
      }

      /*!
       * \brief The pattern/text encoding.
       * \return The configured encoding.
       */
      [[nodiscard]] Encoding encoding() const noexcept
      {
        return encoding_;
      }

      /*!
       * \brief Sets `encoding`. `EncodingLatin1` makes the owning `RE2` reject at construction.
       * \param[in] value The new encoding.
       */
      void set_encoding(Encoding value) noexcept
      {
        encoding_ = value;
      }

      /*!
       * \brief Whether the pattern is restricted to POSIX egrep syntax; `true` is rejected at construction.
       * \return Whether POSIX-ERE syntax is selected.
       */
      [[nodiscard]] bool posix_syntax() const noexcept
      {
        return posix_syntax_;
      }

      /*!
       * \brief Sets `posix_syntax`.
       * \param[in] value `true` makes the owning `RE2` reject at construction.
       */
      void set_posix_syntax(bool value) noexcept
      {
        posix_syntax_ = value;
      }

      /*!
       * \brief Whether to search for the longest match instead of the first; honored by every unanchored search.
       * \return Whether leftmost-longest semantics are selected.
       */
      [[nodiscard]] bool longest_match() const noexcept
      {
        return longest_match_;
      }

      /*!
       * \brief Sets `longest_match`.
       * \param[in] value The new setting.
       */
      void set_longest_match(bool value) noexcept
      {
        longest_match_ = value;
      }

      /*!
       * \brief Whether compile errors are logged. Reads `true` by default, as RE2's does; logging itself is
       *        opt-in here (see \ref set_log_errors), and `RE2::ok()`/`RE2::error()` report errors either way.
       * \return The setting.
       */
      [[nodiscard]] bool log_errors() const noexcept
      {
        return log_errors_;
      }

      /*!
       * \brief Sets `log_errors`. Only an explicit `set_log_errors(true)` arms RE2's stderr message on a failed
       *        compile; `set_log_errors(false)` disarms it.
       * \param[in] value The new setting.
       */
      void set_log_errors(bool value) noexcept
      {
        log_errors_       = value;
        log_errors_armed_ = value;
      }

      /*!
       * \brief Whether the pattern is matched as a literal string rather than parsed as a regex.
       *        Honored by escaping the pattern (`QuoteMeta`) before compiling.
       * \return Whether the pattern is treated as a literal string.
       */
      [[nodiscard]] bool literal() const noexcept
      {
        return literal_;
      }

      /*!
       * \brief Sets `literal`.
       * \param[in] value The new setting.
       */
      void set_literal(bool value) noexcept
      {
        literal_ = value;
      }

      /*!
       * \brief Whether the pattern must never match `\n`; `true` is rejected at construction.
       * \return Whether `.` and negated classes are barred from matching a newline.
       */
      [[nodiscard]] bool never_nl() const noexcept
      {
        return never_nl_;
      }

      /*!
       * \brief Sets `never_nl`.
       * \param[in] value `true` makes the owning `RE2` reject at construction.
       */
      void set_never_nl(bool value) noexcept
      {
        never_nl_ = value;
      }

      /*!
       * \brief Whether `.` also matches `\n`. Honored via `flags::dotall`.
       * \return Whether `.` matches a newline.
       */
      [[nodiscard]] bool dot_nl() const noexcept
      {
        return dot_nl_;
      }

      /*!
       * \brief Sets `dot_nl`.
       * \param[in] value The new setting.
       */
      void set_dot_nl(bool value) noexcept
      {
        dot_nl_ = value;
      }

      /*!
       * \brief Whether every `(...)` parses as non-capturing; `true` is rejected at construction, since ignoring
       *        it would change `NumberOfCapturingGroups()` and the `Arg` extractions.
       * \return Whether capturing groups are demoted to non-capturing.
       */
      [[nodiscard]] bool never_capture() const noexcept
      {
        return never_capture_;
      }

      /*!
       * \brief Sets `never_capture`.
       * \param[in] value `true` makes the owning `RE2` reject at construction.
       */
      void set_never_capture(bool value) noexcept
      {
        never_capture_ = value;
      }

      /*!
       * \brief Whether matching is case-sensitive (a pattern's `(?i)` overrides it, as in RE2).
       * \return Whether matching is case-sensitive.
       */
      [[nodiscard]] bool case_sensitive() const noexcept
      {
        return case_sensitive_;
      }

      /*!
       * \brief Sets `case_sensitive`.
       * \param[in] value The new setting.
       */
      void set_case_sensitive(bool value) noexcept
      {
        case_sensitive_ = value;
      }

      /*!
       * \brief Read by RE2 only under `posix_syntax`, which this layer rejects: inert, as in RE2 without it.
       * \return Whether Perl classes (`\\d`, `\\s`, `\\w`) are enabled.
       */
      [[nodiscard]] bool perl_classes() const noexcept
      {
        return perl_classes_;
      }

      /*!
       * \brief Sets `perl_classes` (inert; see the getter).
       * \param[in] value Stored, not consulted.
       */
      void set_perl_classes(bool value) noexcept
      {
        perl_classes_ = value;
      }

      /*!
       * \brief Inert, as \ref perl_classes is.
       * \return Whether `\\b` and `\\B` are enabled.
       */
      [[nodiscard]] bool word_boundary() const noexcept
      {
        return word_boundary_;
      }

      /*!
       * \brief Sets `word_boundary` (inert; see the getter).
       * \param[in] value Stored, not consulted.
       */
      void set_word_boundary(bool value) noexcept
      {
        word_boundary_ = value;
      }

      /*!
       * \brief Inert, as \ref perl_classes is.
       * \return Whether `^`/`$` match only at the text edges.
       */
      [[nodiscard]] bool one_line() const noexcept
      {
        return one_line_;
      }

      /*!
       * \brief Sets `one_line` (inert; see the getter).
       * \param[in] value Stored, not consulted.
       */
      void set_one_line(bool value) noexcept
      {
        one_line_ = value;
      }

    private:

      friend class RE2;                               // reads log_errors_armed_, which is not part of RE2's Options interface

      std::int64_t  max_mem_          {8 << 20};      //!< RE2's default budget (see \ref max_mem).
      Encoding      encoding_         {EncodingUTF8}; //!< The pattern/text encoding.
      bool          posix_syntax_     {false};        //!< POSIX egrep syntax restriction.
      bool          longest_match_    {false};        //!< Leftmost-longest vs leftmost-first.
      bool          log_errors_       {true};         //!< The `log_errors` setting, `true` by default as RE2's.
      bool          log_errors_armed_ {false};        //!< Set by an explicit `set_log_errors(true)`: only then is anything logged.
      bool          literal_          {false};        //!< Match the pattern as a literal string.
      bool          never_nl_         {false};        //!< Never match `\n`.
      bool          dot_nl_           {false};        //!< `.` also matches `\n`.
      bool          never_capture_    {false};        //!< Parse every `(...)` as non-capturing.
      bool          case_sensitive_   {true};         //!< Case-sensitive by default.
      bool          perl_classes_     {false};        //!< POSIX-mode-only; inert here.
      bool          word_boundary_    {false};        //!< POSIX-mode-only; inert here.
      bool          one_line_         {false};        //!< POSIX-mode-only; inert here.
    };

    /*!
     * \brief A coarse error taxonomy, not RE2's 14 codes (REAL's parser classifies differently); `error()`
     *        carries the detail.
     */
    enum class ErrorCode : std::uint8_t
    {
      NoError,              //!< `ok() == true`.
      ErrorSyntax,          //!< The pattern is malformed (rejected by RE2 too).
      ErrorUnsupported,     //!< Well-formed, but outside this layer's supported subset (see `error()`).
      ErrorPatternTooLarge, //!< The compiled pattern exceeds `max_mem`, or REAL's own program bound.
    };

    /*!
     * \brief Where a `Set` member (or the primitive `Match`, not exposed here) is anchored.
     */
    enum class Anchor : std::uint8_t
    {
      UNANCHORED,   //!< Match anywhere in the text.
      ANCHOR_START, //!< Match must start at the beginning of the text.
      ANCHOR_BOTH,  //!< Match must span the entire text.
    };

    static constexpr Anchor UNANCHORED              {Anchor::UNANCHORED};              //!< As RE2's `RE2::UNANCHORED`.
    static constexpr Anchor ANCHOR_START            {Anchor::ANCHOR_START};            //!< As RE2's `RE2::ANCHOR_START`.
    static constexpr Anchor ANCHOR_BOTH             {Anchor::ANCHOR_BOTH};             //!< As RE2's `RE2::ANCHOR_BOTH`.

    static constexpr ErrorCode NoError              {ErrorCode::NoError};              //!< As RE2's `RE2::NoError`.
    static constexpr ErrorCode ErrorSyntax          {ErrorCode::ErrorSyntax};          //!< This layer's syntax category.
    static constexpr ErrorCode ErrorUnsupported     {ErrorCode::ErrorUnsupported};     //!< This layer's unsupported category.
    static constexpr ErrorCode ErrorPatternTooLarge {ErrorCode::ErrorPatternTooLarge}; //!< As RE2's `RE2::ErrorPatternTooLarge`.

    /*!
     * \brief `RE2::Set`: `Add` patterns, `Compile` once, then `Match` repeatedly, on `real::regex_set::which()`.
     *        The anchor is applied by wrapping each pattern, `real::regex_set` being unanchored.
     */
    class Set
    {
    public:

      /*!
       * \brief Starts an empty set.
       * \param[in] options The options applied to every member pattern.
       * \param[in] anchor  The anchor mode every member is matched with.
       */
      Set(const Options&  options,
          Anchor          anchor)
        : options_(options),
          anchor_(anchor)
      {}

      /*!
       * \brief Buffers one pattern (validated immediately, like real RE2's `Add`).
       * \param[in]  pattern The pattern text.
       * \param[out] error   If non-null and the pattern is rejected, set to a human-readable reason.
       * \return The pattern's index (matches `Match`'s output indices) on success, or `-1`.
       */
      int Add(std::string_view pattern,
              std::string*     error)
      {
        const std::string_view reason {RE2::unsupported_option(options_)};
        if (!reason.empty()) {
          if (error != nullptr) {
            *error = std::string(reason);
          }
          RE2::log_error(options_, "Error parsing", pattern, reason); // RE2's Set prints the pattern whole
          return -1;
        }
        std::string wrapped {anchor_wrap(detail::translate_flavor(pattern, !options_.case_sensitive()))};
        try {
          const real::regex probe(wrapped, RE2::options_to_flags(options_));
          program_bytes_        += RE2::program_bytes(probe);
          program_instructions_ += probe.raw_program().code.size();
        } catch (const real::regex_error& e) {
          if (error != nullptr) {
            *error = e.what();
          }
          RE2::log_error(options_, "Error parsing", pattern, e.what());
          return -1;
        }
        const int index {static_cast<int>(patterns_.size())};
        patterns_.push_back(std::move(wrapped));
        return index;
      }

      /*!
       * \brief Compiles every buffered pattern into one `real::regex_set`.
       * \return `true` on success; `false` when the members together exceed `max_mem`, as RE2's own
       *         `Compile` fails (each member was already probe-compiled by `Add`).
       */
      bool Compile()
      {
        if (!RE2::within_budget(program_bytes_, program_instructions_, options_.max_mem())) {
          return false;
        }
        try {
          set_.emplace(patterns_, RE2::options_to_flags(options_));
        } catch (const real::regex_error&) {
          return false;
        }
        return true;
      }

      /*!
       * \brief Tests \p text against every compiled member.
       * \param[in]  text The subject text.
       * \param[out] v    If non-null, cleared and filled with the indices of every matching member.
       * \return `true` if at least one member matched.
       */
      [[nodiscard]] bool Match(std::string_view   text,
                               std::vector<int> * v) const
      {
        if (!set_.has_value()) {
          return false;
        }
        const std::vector<std::size_t> hits {set_->which(text)};
        if (v != nullptr) {
          v->clear();
          v->reserve(hits.size());
          for (const std::size_t index : hits) {
            v->push_back(static_cast<int>(index));
          }
        }
        return !hits.empty();
      }

    private:

      /*!
       * \brief Synthesizes `anchor_`'s semantics by wrapping \p pattern, since `real::regex_set`
       *        is natively unanchored.
       * \param[in] pattern The raw member pattern text.
       * \return The pattern text to actually compile.
       */
      [[nodiscard]] std::string anchor_wrap(std::string_view pattern) const
      {
        switch (anchor_) {
          case Anchor::ANCHOR_START:
            return "^(?:" + std::string(pattern) + ")";
          case Anchor::ANCHOR_BOTH:
            return "^(?:" + std::string(pattern) + ")$";
          case Anchor::UNANCHORED:
          default:
            return std::string(pattern);
        }
      }

      Options                             options_;                  //!< Options shared by every member.
      Anchor                              anchor_;                   //!< The anchor mode every member is matched with.
      std::vector<std::string>            patterns_;                 //!< Buffered, already anchor-wrapped member patterns.
      std::optional<real::regex_set>      set_;                      //!< Engaged once `Compile` succeeds.
      std::size_t                         program_bytes_        {0}; //!< The members' programs, in bytes (`RE2::program_bytes`).
      std::size_t                         program_instructions_ {0}; //!< The members' instructions.
    };

    /*!
     * \brief Compiles \p pattern with default options. Implicit, like real RE2's own pattern
     *        constructors — so `FullMatch(text, "a.*b", &arg)` builds a temporary `RE2` in place.
     * \param[in] pattern The pattern text (NUL-terminated).
     */
    RE2(const char* pattern)
      : RE2(std::string_view(pattern),
            Options())
    {}

    /*!
     * \brief Compiles \p pattern with default options. Implicit; see the `const char*` overload.
     *
     * Not mergeable into the `std::string_view` overload: a `std::string` would then need two user-defined
     * conversions to become an `RE2`, and `FullMatch(text, some_string, &arg)` would stop compiling.
     * \param[in] pattern The pattern text.
     */
    RE2(const std::string& pattern)
      : RE2(std::string_view(pattern),
            Options())
    {}

    /*!
     * \brief Compiles \p pattern with default options. Implicit; see the `const char*` overload.
     * \param[in] pattern The pattern text.
     */
    RE2(std::string_view pattern)
      : RE2(pattern,
            Options())
    {}

    /*!
     * \brief Compiles \p pattern with \p options.
     * \param[in] pattern The pattern text.
     * \param[in] options The construction options.
     */
    RE2(std::string_view  pattern,
        const Options&    options)
      : pattern_(pattern),
        options_(options)
    {
      init(pattern, options);
    }

    /*!
     * \brief Whether construction succeeded (`error_code() == ErrorCode::NoError`).
     * \return Whether the pattern compiled.
     */
    [[nodiscard]] bool ok() const noexcept
    {
      return error_code_ == ErrorCode::NoError;
    }

    /*!
     * \brief The pattern text this `RE2` was built from.
     * \return The pattern, valid as long as this object is alive.
     */
    [[nodiscard]] const std::string& pattern() const noexcept
    {
      return pattern_;
    }

    /*!
     * \brief The rejection reason, or an empty string if `ok()`.
     * \return The message, empty when construction succeeded.
     */
    [[nodiscard]] const std::string& error() const noexcept
    {
      return error_;
    }

    /*!
     * \brief The coarse rejection category, or `ErrorCode::NoError` if `ok()`.
     * \return The category, `ErrorCode::NoError` when construction succeeded.
     */
    [[nodiscard]] ErrorCode error_code() const noexcept
    {
      return error_code_;
    }

    /*!
     * \brief The construction options this `RE2` was built with.
     * \return The options, valid as long as this object is alive.
     */
    [[nodiscard]] const Options& options() const noexcept
    {
      return options_;
    }

    /*!
     * \brief The number of capturing groups (excluding group 0), or `0` if `!ok()`.
     * \return The capturing-group count.
     */
    [[nodiscard]] int NumberOfCapturingGroups() const noexcept
    {
      return num_captures_;
    }

    /*!
     * \brief Anchored-both match: the whole \p text must match \p re.
     * \tparam    Args Destination pointer types (deduced), one per submatch extracted.
     * \param[in]  text The subject text.
     * \param[in]  re   The pattern (an `RE2`, or a pattern text implicitly converted to one).
     * \param[out] args Destinations for the first `sizeof...(Args)` capturing groups (see `Arg`);
     *                  pass none to just test for a match.
     * \return `true` on a full match with every \p args extraction succeeding.
     */
    template <typename ... Args>
    [[nodiscard]] static bool FullMatch(std::string_view  text,
                                        const RE2&        re,
                                        Args&&...         args)
    {
      if (!re.ok()) {
        return false;
      }
      return extract(re.regex_->fullmatch(text), std::forward<Args>(args)...);
    }

    /*!
     * \brief Unanchored match: \p re must match some substring of \p text.
     * \tparam    Args Destination pointer types (deduced), one per submatch extracted.
     * \param[in]  text The subject text.
     * \param[in]  re   The pattern (an `RE2`, or a pattern text implicitly converted to one).
     * \param[out] args Destinations for the first `sizeof...(Args)` capturing groups (see `Arg`);
     *                  pass none to just test for a match.
     * \return `true` on a match with every \p args extraction succeeding.
     */
    template <typename ... Args>
    [[nodiscard]] static bool PartialMatch(std::string_view  text,
                                           const RE2&        re,
                                           Args&&...         args)
    {
      if (!re.ok()) {
        return false;
      }
      return extract(re.longest_match_ ? re.regex_->search_longest(text) : re.regex_->search(text),
                     std::forward<Args>(args)...);
    }

    /*!
     * \brief Anchored-start match against `*input`; on success, removes the matched prefix from
     *        `*input`.
     * \tparam    Args Destination pointer types (deduced), one per submatch extracted.
     * \param[in,out] input The subject text; shrunk from the front on a successful match.
     * \param[in]     re    The pattern (an `RE2`, or a pattern text implicitly converted to one).
     * \param[out]    args  Destinations for the first `sizeof...(Args)` capturing groups (see `Arg`).
     * \return `true` on a match with every \p args extraction succeeding.
     */
    template <typename ... Args>
    static bool Consume(std::string_view * input,
                        const RE2&         re,
                        Args&&...          args)
    {
      if (input == nullptr || !re.ok()) {
        return false;
      }
      const auto match {re.regex_->match(*input)};
      if (!extract(match, std::forward<Args>(args)...)) {
        return false;
      }
      *input = input->substr(match.end());
      return true;
    }

    /*!
     * \brief Unanchored match anywhere in `*input`; on success, removes everything up to and
     *        including the match from `*input`.
     * \tparam    Args Destination pointer types (deduced), one per submatch extracted.
     * \param[in,out] input The subject text; shrunk from the front on a successful match.
     * \param[in]     re    The pattern (an `RE2`, or a pattern text implicitly converted to one).
     * \param[out]    args  Destinations for the first `sizeof...(Args)` capturing groups (see `Arg`).
     * \return `true` on a match with every \p args extraction succeeding.
     */
    template <typename ... Args>
    static bool FindAndConsume(std::string_view * input,
                               const RE2&         re,
                               Args&&...          args)
    {
      if (input == nullptr || !re.ok()) {
        return false;
      }
      const auto match {re.longest_match_ ? re.regex_->search_longest(*input) : re.regex_->search(*input)};
      if (!extract(match, std::forward<Args>(args)...)) {
        return false;
      }
      *input = input->substr(match.end());
      return true;
    }

    /*!
     * \brief Replaces the first match of \p re in `*str` with \p rewrite.
     *
     * \p rewrite may reference groups RE2-style: `\0` (whole match), `\1`…`\9`, `\\` for a literal
     * backslash.
     * \param[in,out] str     The subject; rewritten in place only on success.
     * \param[in]     re      The pattern (an `RE2`, or a pattern text implicitly converted to one).
     * \param[in]     rewrite The replacement template.
     * \return `true` if a match was found and \p rewrite was well-formed.
     */
    static bool Replace(std::string *      str,
                        const RE2&         re,
                        std::string_view   rewrite)
    {
      if (str == nullptr || !re.ok()) {
        return false;
      }
      const auto match {re.longest_match_ ? re.regex_->search_longest(*str) : re.regex_->search(*str)};
      if (!match.matched()) {
        return false;
      }
      std::string out;
      out.reserve(str->size());
      out.append(std::string_view {*str}.substr(0, match.start())); // views: a string's substr is a copy
      if (!expand_rewrite(out, rewrite, match)) {
        return false;
      }
      out.append(std::string_view {*str}.substr(match.end()));
      *str = std::move(out);
      return true;
    }

    /*!
     * \brief Replaces every non-overlapping match of \p re in `*str` with \p rewrite.
     *
     * \p rewrite may reference groups RE2-style: `\0` (whole match), `\1`…`\9`, `\\` for a literal
     * backslash.
     *
     * As in RE2, an empty match starting where the previous match ended is skipped (`a*` over `aa` gives one
     * replacement); other empty matches are replaced (`a*` over `bbb` gives `#b#b#b#`).
     * \param[in,out] str     The subject; rewritten in place only if every match's \p rewrite
     *                        expansion succeeds.
     * \param[in]     re      The pattern (an `RE2`, or a pattern text implicitly converted to one).
     * \param[in]     rewrite The replacement template.
     * \return The number of replacements made (`0` if none, or if \p rewrite was malformed).
     */
    static int GlobalReplace(std::string *      str,
                             const RE2&         re,
                             std::string_view   rewrite)
    {
      if (str == nullptr || !re.ok()) {
        return 0;
      }
      std::string            out;         // sized at the first match: a miss allocates nothing
      const std::string_view text {*str};
      std::size_t            last     {}; // where the text not yet copied starts
      std::size_t            pos      {}; // where the next search starts
      std::size_t            last_end {real::npos};
      int                    count    {};
      // RE2's walk, not Python's: after an empty match the next search starts one character on (no second
      // try for a longer match at the same place), and an empty match where the previous match ended is
      // skipped, one whole UTF-8 character at a time.
      while (pos <= text.size()) {
        const auto match {re.longest_match_ ? re.regex_->search_longest(text, pos) : re.regex_->search(text, pos)};
        if (!match) {
          break;
        }
        if (match.start() == match.end() && match.start() == last_end) {
          pos = match.start() + detail::next_character_length(text, match.start());
          continue;
        }
        if (count == 0) {
          out.reserve(text.size());
        }
        out.append(text.substr(last, match.start() - last)); // a view: a string's substr is a copy
        if (!expand_rewrite(out, rewrite, match)) {
          return 0;
        }
        last     = match.end();
        last_end = match.end();
        pos      = match.end();
        ++count;
      }
      if (count == 0) {
        return 0;
      }
      out.append(text.substr(last));
      *str = std::move(out);
      return count;
    }

    /*!
     * \brief Escapes every regex metacharacter in \p unquoted so the result matches it literally.
     * \param[in] unquoted The raw text.
     * \return The escaped pattern text.
     */
    [[nodiscard]] static std::string QuoteMeta(std::string_view unquoted)
    {
      std::string result;
      result.reserve(unquoted.size() * 2);
      for (const unsigned char c : unquoted) {
        const bool safe {(c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                         c == '_' || (c & 0x80U) != 0};
        if (safe) {
          result.push_back(static_cast<char>(c));
        }
        else if (c == '\0') {
          result.append("\\x00"); // a literal backslash-NUL would misparse when re-read as a C string
        }
        else {
          result.push_back('\\');
          result.push_back(static_cast<char>(c));
        }
      }
      return result;
    }

  private:

    static constexpr std::string_view pattern_too_large {"pattern too large - compile failed"}; //!< RE2's text for `ErrorPatternTooLarge`.

    /*!
     * \brief The bytes of \p re's compiled program: every table it holds, as RE2 counts its program (the
     *        pattern's own copy and the tables' spare capacity are not counted).
     * \param[in] re A compiled pattern.
     * \return Its size in bytes.
     */
    [[nodiscard]] static std::size_t program_bytes(const real::regex& re) noexcept
    {
      const real::detail::program_view p {re.raw_program()};
      return sizeof(real::detail::dynamic_program) + p.code.size_bytes() + p.classes.size_bytes()
             + p.names.size_bytes() + p.lookarounds.size_bytes() + p.cp_classes.size_bytes() + p.cp_ranges.size_bytes()
             + p.prefix_code.size_bytes() + p.prefix_classes.size_bytes() + p.prefix_cp_classes.size_bytes()
             + p.prefix_cp_ranges.size_bytes();
    }

    /*!
     * \brief Whether a program of \p bytes and \p instructions fits \p max_mem as RE2 bounds it: two thirds
     *        of the budget for the program, and 100 000 instructions when those two thirds are not positive.
     * \param[in] bytes        The program's size (\ref program_bytes).
     * \param[in] instructions Its instruction count.
     * \param[in] max_mem      The budget (\ref Options::max_mem).
     * \return True when it fits.
     */
    [[nodiscard]] static constexpr bool within_budget(std::size_t  bytes,
                                                      std::size_t  instructions,
                                                      std::int64_t max_mem) noexcept
    {
      const std::int64_t program_budget {(max_mem / 3 * 2) + (max_mem % 3 * 2 / 3)}; // max_mem * 2 / 3, no overflow
      if (program_budget <= 0) {
        return instructions <= 100000U;
      }
      return bytes <= static_cast<std::uint64_t>(program_budget);
    }

    /*!
     * \brief Translates the `Options` fields this layer honors into `real::flags`.
     * \param[in] options The options to translate.
     * \return The equivalent \ref real::flags set.
     */
    [[nodiscard]] static real::flags options_to_flags(const Options& options)
    {
      // allow_raw_byte always: `\C` is safe on this byte-offset API (see the file comment). dollar_endonly
      // always: RE2's `$` without `(?m)` is the end of the text, never before a final newline.
      real::flags result {real::flags::allow_raw_byte | real::flags::dollar_endonly};
      if (!options.case_sensitive()) {
        result = result | real::flags::icase;
      }
      if (options.dot_nl()) {
        result = result | real::flags::dotall;
      }
      return result;
    }

    /*!
     * \brief The first unsupported-option rejection reason for \p options, if any.
     * \param[in] options The options to check.
     * \return A human-readable reason, or an empty view if every option is supported.
     */
    [[nodiscard]] static std::string_view unsupported_option(const Options& options)
    {
      if (options.encoding() == Options::EncodingLatin1) {
        return "real::compat::re2: Latin1 encoding is not supported (UTF-8 default mode only)";
      }
      if (options.posix_syntax()) {
        return "real::compat::re2: posix_syntax is not supported";
      }
      if (options.never_nl()) {
        return "real::compat::re2: never_nl is not supported";
      }
      if (options.never_capture()) {
        return "real::compat::re2: never_capture is not supported";
      }
      return {};
    }

    /*!
     * \brief Writes the message RE2 writes to stderr on a failed compile, once an explicit
     *        `set_log_errors(true)` armed the logging: "Error parsing" with the pattern and the error for a
     *        pattern it cannot parse, "Error compiling" with the pattern for one too large.
     * \param[in] options The options the compile ran under.
     * \param[in] what    "Error parsing" or "Error compiling".
     * \param[in] pattern The pattern, as RE2 prints it.
     * \param[in] detail  The error, or empty for none.
     */
    static void log_error(const Options&   options,
                          std::string_view what,
                          std::string_view pattern,
                          std::string_view detail)
    {
      if (!options.log_errors_armed_) {
        return;
      }
      std::string line {what};
      line += " '";
      line += pattern;
      line += "'";
      if (!detail.empty()) {
        line += ": ";
        line += detail;
      }
      line += '\n';
      static_cast<void>(std::fputs(line.c_str(), stderr));
    }

    /*!
     * \brief The pattern as RE2 prints it in a compile error: its first 100 bytes and `...` past that.
     * \param[in] pattern The pattern.
     * \return The printed form.
     */
    [[nodiscard]] static std::string trunc(std::string_view pattern)
    {
      return pattern.size() < 100U ? std::string {pattern} : std::string {pattern.substr(0, 100)} + "...";
    }

    /*!
     * \brief Compiles `pattern_` under `options`, or records why it could not be compiled.
     * \param[in] pattern The pattern text (pre-`literal()`-expansion).
     * \param[in] options The construction options.
     */
    void init(std::string_view  pattern,
              const Options&    options)
    {
      const std::string_view reason {unsupported_option(options)};
      if (!reason.empty()) {
        error_      = reason;
        error_code_ = ErrorCode::ErrorUnsupported;
        log_error(options, "Error parsing", trunc(pattern), error_);
        return;
      }
      longest_match_ = options.longest_match();
      const std::string effective {options.literal() ? QuoteMeta(pattern)
                                                     : detail::translate_flavor(pattern, !options.case_sensitive())};
      try {
        regex_.emplace(effective, options_to_flags(options));
      } catch (const real::regex_error& e) {
        if (e.cause() == real::detail::program_too_large) {
          error_      = std::string {pattern_too_large};
          error_code_ = ErrorCode::ErrorPatternTooLarge;
          log_error(options, "Error compiling", trunc(pattern), {});
          return;
        }
        error_      = e.what();
        error_code_ = e.kind() == real::error_kind::unsupported ? ErrorCode::ErrorUnsupported
                                                                 : ErrorCode::ErrorSyntax;
        log_error(options, "Error parsing", trunc(pattern), error_);
        return;
      }
      if (!within_budget(program_bytes(*regex_), regex_->raw_program().code.size(), options.max_mem())) {
        regex_.reset();
        error_      = std::string {pattern_too_large};
        error_code_ = ErrorCode::ErrorPatternTooLarge;
        log_error(options, "Error compiling", trunc(pattern), {});
        return;
      }
      num_captures_ = static_cast<int>(regex_->group_count());
    }

    /*!
     * \brief Extracts \p args from a match result, RE2's own `DoMatch` contract: too few groups,
     *        no match, or any one extraction failing fails the whole call.
     * \tparam    Result The match-result type (`real::regex::result_type`).
     * \tparam    Args   Destination pointer types (deduced).
     * \param[in]  match The match attempt's result.
     * \param[out] args  Destinations for the first `sizeof...(Args)` capturing groups.
     * \return `true` on a match with every extraction succeeding.
     */
    template <typename Result, typename ... Args>
    [[nodiscard]] static bool extract(const Result& match,
                                      Args&&...     args)
    {
      if (!match.matched()) {
        return false;
      }
      constexpr std::size_t count {sizeof...(Args)};
      if constexpr (count == 0) {
        return true;
      }
      else {
        if (match.size() <= count) {
          return false; // not enough capturing groups for the requested extraction (group 0 + count)
        }
        const Arg    cells[] {Arg(std::forward<Args>(args))...};
        std::size_t  group   {1};
        for (const Arg& cell : cells) {
          const std::string_view piece {match[group]};
          if (!cell.parse(piece.data(), piece.size())) {
            return false;
          }
          ++group;
        }
        return true;
      }
    }

    /*!
     * \brief Expands an RE2-style rewrite template (`\0`…`\9`, `\\`) against \p match, appending
     *        to \p out.
     * \tparam    Result  The match-result type (`real::regex::result_type`).
     * \param[out] out     Appended to on success (left unspecified on failure).
     * \param[in]  rewrite The rewrite template.
     * \param[in]  match   The match whose groups `\N` refers to.
     * \return `true` if \p rewrite was well-formed (every `\N` in range, no trailing backslash).
     */
    template <typename Result>
    [[nodiscard]] static bool expand_rewrite(std::string&       out,
                                             std::string_view   rewrite,
                                             const Result&      match)
    {
      for (std::size_t i {}; i < rewrite.size(); ++i) {
        const char c {rewrite[i]};
        if (c != '\\') {
          out.push_back(c);
          continue;
        }
        ++i;
        if (i >= rewrite.size()) {
          return false; // trailing backslash
        }
        const char next {rewrite[i]};
        if (next == '\\') {
          out.push_back('\\');
        }
        else if (next >= '0' && next <= '9') {
          const std::size_t group {static_cast<std::size_t>(next - '0')};
          if (group >= match.size()) {
            return false; // group reference out of range
          }
          out.append(match[group]);
        }
        else {
          return false; // an invalid escape in the rewrite template
        }
      }
      return true;
    }

    std::optional<real::regex>  regex_;                              //!< Engaged only if compilation succeeded.
    std::string                 pattern_;                            //!< The original pattern text.
    std::string                 error_;                              //!< The rejection reason, if any.
    ErrorCode                   error_code_    {ErrorCode::NoError}; //!< The rejection category.
    int                         num_captures_  {};                   //!< Capturing-group count (excl. group 0).
    bool                        longest_match_ {};                   //!< Cached `options_.longest_match()`.
    Options                     options_;                            //!< The construction options.
  };

  /*!
   * \brief RE2's `LazyRE2`: a pattern compiled on first use, once, thread-safely -- `static LazyRE2 re = {"a+"};`
   *        then `RE2::FullMatch(text, *re)`. An aggregate, as RE2's, so the members are public; only
   *        \ref pattern_ and \ref options_ are meant to be set, by that brace initialisation.
   */
  struct LazyRE2
  {
    const char*                pattern_;                        //!< The pattern text.
    RE2::CannedOptions         options_  {RE2::DefaultOptions}; //!< The options it is compiled with.
    //! Set once, on first use. Its `{}` spares a caller writing `{.pattern_ = ...}` a missing-initializer warning.
    mutable std::optional<RE2> compiled_ {};                    // NOLINT(readability-redundant-member-init)
    mutable std::once_flag     once_     {};                    //!< Guards the first use.

    /*!
     * \brief The compiled pattern.
     * \return It, compiled on the first call.
     */
    const RE2& operator*() const
    {
      return *get();
    }

    /*!
     * \brief The compiled pattern.
     * \return A pointer to it, compiled on the first call.
     */
    const RE2* operator->() const
    {
      return get();
    }

    /*!
     * \brief The compiled pattern, compiled on the first call from any thread.
     * \return A pointer to it.
     */
    const RE2* get() const
    {
      std::call_once(once_, [this] { compiled_.emplace(std::string_view {pattern_}, RE2::Options {options_}); });
      return &*compiled_;
    }
  };
} // namespace real::compat::re2

#endif // REAL_RE2_RE2_HPP
