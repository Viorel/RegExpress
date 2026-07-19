/*!
 * \file re2/re2.hpp
 * \brief `real::compat::re2` — an RE2-compatible drop-in (issue #2), the `RE2` class surface.
 *
 * `#include <real/compat/re2/re2.hpp>` is the one public entry point (it pulls in `re2/arg.hpp`
 * for `RE2::Arg`). Mirrors real RE2's own `re2.h` naming exactly — `FullMatch`, `PartialMatch`,
 * `QuoteMeta`, `set_longest_match`, `ANCHOR_START`, … — spelled the way an RE2 user already
 * types them, not translated to REAL's own naming conventions; that is the entire point of a
 * drop-in.
 *
 * REAL is a near-total syntax superset of RE2: it compiles everything RE2 compiles, plus bounded
 * lookarounds and possessive quantifiers (which RE2 rejects outright) and a wider Unicode property
 * set — namespaced `\p{gc=…}`/`\p{sc=…}`/`\p{scx=…}` and the UCD binary properties
 * (`\p{Alphabetic}`, `\p{Emoji}`, …), which RE2's `\p{…}` grammar lacks. Two constructs are the
 * deliberate exceptions where REAL is intentionally *stricter* than RE2 — principled divergences,
 * not unimplemented gaps: (1) **duplicate capturing-group names** (`(?P<n>…)(?P<n>…)`) — RE2
 * tolerates them; REAL rejects them because its named-capture lookup (match-by-name) would be
 * ambiguous with a repeated name (capture-safety); (2) **surrogate code points** in
 * `\x{…}`/`\u`/`\U`/`\N` (e.g. `\x{D800}`) — RE2 does not validate them, REAL rejects them through
 * the same Unicode-scalar validation it applies to every code-point escape. Both surface as a
 * clean `ok() == false`, and are the only two entries in this layer's `fuzz_re2` KNOWN-GAP ledger.
 * So this wraps `real::regex` / `real::regex_set` directly: **no RE2 dependency at runtime**
 * (header-only, zero-dep; RE2 is a test-time oracle only, never linked by this header or anything
 * it includes).
 *
 * **No fallback policy.** The `std::regex` compat layer can delegate an ineligible pattern to
 * `std::regex` (`policy::fallback`) because `std::regex` is always there. RE2 is not a runtime
 * dependency here, so there is nothing to fall back to: a pattern or option this layer cannot
 * honor is a clean, immediate rejection (`ok() == false`, `error()` explains why) — the same
 * shape RE2 itself uses for a syntax error, so callers already know how to handle it. RE2 itself
 * has no exceptions (`RE2 re("(broken"); if (!re.ok()) …`); this layer follows that contract
 * rather than std::regex's throwing one.
 *
 * **Scope.** Matches RE2's *default* mode (`Options::posix_syntax() == false`,
 * `Options::encoding() == Options::EncodingUTF8`) — REAL's own text mode is the documented parity
 * point (codepoint-aware, `flags::ecma`-free since RE2 is Perl-flavored, not ECMAScript-flavored).
 * `posix_syntax`, Latin-1, `never_nl`, and `never_capture` have no REAL-side equivalent and are
 * rejected at construction (`ErrorUnsupported`) rather than silently ignored; `perl_classes`,
 * `word_boundary`, and `one_line` are inert here for the same reason they are inert in real RE2
 * outside `posix_syntax` mode (its own documented behavior, not a new divergence). `longest_match`
 * is honored by every unanchored-search operation — `PartialMatch`, `FindAndConsume`, `Replace`,
 * `GlobalReplace` — via `real::regex::search_longest()`/`find_iter_longest()`, REAL's own,
 * pre-existing, documented RE2 `set_longest_match` equivalents; `FullMatch`/`Consume`'s anchored
 * boundaries do not depend on it (matching text is matching text end-to-end either way), and its
 * effect on capture tie-breaking inside an ambiguous alternation is the same non-POSIX-submatch
 * caveat RE2 itself documents as shared, not a new one.
 *
 * **`\C` (RE2's raw-byte escape) is accepted**, matching real RE2's own default-mode behavior
 * exactly: it consumes exactly one byte, unconditionally, possibly landing mid-codepoint. Safe
 * here specifically because this layer's whole API is byte-offset C++ (mirroring RE2's own) — the
 * char-offset hazard that keeps `\C` gated to `flags::bytes` on REAL-native, char-offset surfaces
 * (e.g. the Python `str` binding, which cannot reach this flag) never applies to `real::compat::re2`.
 */
#ifndef REAL_RE2_RE2_HPP
#define REAL_RE2_RE2_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp> (or the documented opt-ins <real/dfa.hpp>, <real/compat/std/regex.hpp>,
// <real/compat/re2/re2.hpp>).

#include <real/version.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <real/real.hpp>
#include <real/regex_set.hpp>

#include "arg.hpp"

namespace real::compat::re2 {

  /*!
   * \brief RE2-compatible drop-in for `RE2`. Backed by `real::regex` — linear-time, ReDoS-safe.
   *
   * Copyable and movable (real RE2 deletes both, for pointer-stability reasons this wrapper does
   * not share — `real::regex` copies cheaply). A pattern this layer cannot honor does not throw:
   * construction always succeeds syntactically, and `ok()`/`error()`/`error_code()` report the
   * rejection, exactly mirroring how RE2 itself reports a syntax error.
   */
  class RE2
  {
  public:

    /*!
     * \brief RE2-compatible construction options. Mirrors real RE2's `RE2::Options` field-for-field
     *        (names, defaults); see the file-level doc comment for which fields this layer honors.
     */
    class Options
    {
    public:

      //! \brief Pattern/text encoding — `real::compat::re2` supports `EncodingUTF8` only.
      enum Encoding : std::uint8_t
      {
        EncodingUTF8,  //!< UTF-8 (the default, and the only encoding this layer honors).
        EncodingLatin1 //!< Latin-1 — accepted for API shape, rejected at `RE2` construction.
      };

      //! \brief Default options: UTF-8, leftmost-first, case-sensitive, every RE2 default kept.
      Options() = default;

      //! \brief Approximate memory budget RE2 uses for its compiled program/DFA cache. Stored for API
      //!        shape; REAL has its own, unrelated internal budget knobs and does not consult this.
      [[nodiscard]] std::int64_t max_mem() const noexcept
      {
        return max_mem_;
      }

      //! \brief Sets `max_mem`.
      //! \param[in] value The new budget, in bytes.
      void set_max_mem(std::int64_t value) noexcept
      {
        max_mem_ = value;
      }

      //! \brief The pattern/text encoding.
      [[nodiscard]] Encoding encoding() const noexcept
      {
        return encoding_;
      }

      //! \brief Sets `encoding`. `EncodingLatin1` makes the owning `RE2` reject at construction.
      //! \param[in] value The new encoding.
      void set_encoding(Encoding value) noexcept
      {
        encoding_ = value;
      }

      //! \brief Whether the pattern is restricted to POSIX egrep syntax. Always rejected here (no
      //!        REAL-side equivalent grammar mode) if set to `true`.
      [[nodiscard]] bool posix_syntax() const noexcept
      {
        return posix_syntax_;
      }

      //! \brief Sets `posix_syntax`.
      //! \param[in] value `true` makes the owning `RE2` reject at construction.
      void set_posix_syntax(bool value) noexcept
      {
        posix_syntax_ = value;
      }

      //! \brief Whether to search for the longest match instead of the first. Honored for
      //!        `PartialMatch` (via `real::regex::search_longest()`); see the file-level doc comment.
      [[nodiscard]] bool longest_match() const noexcept
      {
        return longest_match_;
      }

      //! \brief Sets `longest_match`.
      //! \param[in] value The new setting.
      void set_longest_match(bool value) noexcept
      {
        longest_match_ = value;
      }

      //! \brief Whether to log syntax/execution errors. Stored for API shape; this layer reports
      //!        errors through `RE2::error()` regardless, never a logging side-channel.
      [[nodiscard]] bool log_errors() const noexcept
      {
        return log_errors_;
      }

      //! \brief Sets `log_errors`.
      //! \param[in] value The new setting.
      void set_log_errors(bool value) noexcept
      {
        log_errors_ = value;
      }

      //! \brief Whether the pattern is matched as a literal string rather than parsed as a regex.
      //!        Honored by escaping the pattern (`QuoteMeta`) before compiling.
      [[nodiscard]] bool literal() const noexcept
      {
        return literal_;
      }

      //! \brief Sets `literal`.
      //! \param[in] value The new setting.
      void set_literal(bool value) noexcept
      {
        literal_ = value;
      }

      //! \brief Whether the pattern must never match `\n`, even where the pattern text says it
      //!        should. Always rejected here (no REAL-side equivalent) if set to `true`.
      [[nodiscard]] bool never_nl() const noexcept
      {
        return never_nl_;
      }

      //! \brief Sets `never_nl`.
      //! \param[in] value `true` makes the owning `RE2` reject at construction.
      void set_never_nl(bool value) noexcept
      {
        never_nl_ = value;
      }

      //! \brief Whether `.` also matches `\n`. Honored via `flags::dotall`.
      [[nodiscard]] bool dot_nl() const noexcept
      {
        return dot_nl_;
      }

      //! \brief Sets `dot_nl`.
      //! \param[in] value The new setting.
      void set_dot_nl(bool value) noexcept
      {
        dot_nl_ = value;
      }

      //! \brief Whether every `(...)` parses as non-capturing. Always rejected here (this layer
      //!        always captures every group; honoring `true` silently would change
      //!        `NumberOfCapturingGroups()` and which `Arg` extractions succeed) if set to `true`.
      [[nodiscard]] bool never_capture() const noexcept
      {
        return never_capture_;
      }

      //! \brief Sets `never_capture`.
      //! \param[in] value `true` makes the owning `RE2` reject at construction.
      void set_never_capture(bool value) noexcept
      {
        never_capture_ = value;
      }

      //! \brief Whether matching is case-sensitive by default (overridable per-pattern with `(?i)`,
      //!        same interaction RE2 itself documents). Honored via `flags::icase` when `false`.
      [[nodiscard]] bool case_sensitive() const noexcept
      {
        return case_sensitive_;
      }

      //! \brief Sets `case_sensitive`.
      //! \param[in] value The new setting.
      void set_case_sensitive(bool value) noexcept
      {
        case_sensitive_ = value;
      }

      //! \brief Only consulted by real RE2 under `posix_syntax`, which this layer rejects — inert
      //!        here for the same reason it is inert in real RE2 outside `posix_syntax` mode.
      [[nodiscard]] bool perl_classes() const noexcept
      {
        return perl_classes_;
      }

      //! \brief Sets `perl_classes` (inert; see the getter).
      //! \param[in] value Stored, not consulted.
      void set_perl_classes(bool value) noexcept
      {
        perl_classes_ = value;
      }

      //! \brief Only consulted by real RE2 under `posix_syntax`, which this layer rejects — inert
      //!        here for the same reason it is inert in real RE2 outside `posix_syntax` mode.
      [[nodiscard]] bool word_boundary() const noexcept
      {
        return word_boundary_;
      }

      //! \brief Sets `word_boundary` (inert; see the getter).
      //! \param[in] value Stored, not consulted.
      void set_word_boundary(bool value) noexcept
      {
        word_boundary_ = value;
      }

      //! \brief Only consulted by real RE2 under `posix_syntax`, which this layer rejects — inert
      //!        here for the same reason it is inert in real RE2 outside `posix_syntax` mode.
      [[nodiscard]] bool one_line() const noexcept
      {
        return one_line_;
      }

      //! \brief Sets `one_line` (inert; see the getter).
      //! \param[in] value Stored, not consulted.
      void set_one_line(bool value) noexcept
      {
        one_line_ = value;
      }

    private:

      std::int64_t  max_mem_       {8 << 20};      //!< RE2's own default budget; unused by REAL.
      Encoding      encoding_      {EncodingUTF8}; //!< The pattern/text encoding.
      bool          posix_syntax_  {false};        //!< POSIX egrep syntax restriction.
      bool          longest_match_ {false};        //!< Leftmost-longest vs leftmost-first.
      bool          log_errors_    {true};         //!< Log syntax/execution errors (inert here).
      bool          literal_       {false};        //!< Match the pattern as a literal string.
      bool          never_nl_      {false};        //!< Never match `\n`.
      bool          dot_nl_        {false};        //!< `.` also matches `\n`.
      bool          never_capture_ {false};        //!< Parse every `(...)` as non-capturing.
      bool          case_sensitive_{true};         //!< Case-sensitive by default.
      bool          perl_classes_  {false};        //!< POSIX-mode-only; inert here.
      bool          word_boundary_ {false};        //!< POSIX-mode-only; inert here.
      bool          one_line_      {false};        //!< POSIX-mode-only; inert here.
    };

    //! \brief A coarse error taxonomy — this layer does not reproduce RE2's fine-grained,
    //!        14-value `ErrorCode` (REAL's own parser has a different internal classification);
    //!        `error()` always carries the human-readable detail.
    enum class ErrorCode : std::uint8_t
    {
      NoError,          //!< `ok() == true`.
      ErrorSyntax,      //!< The pattern is malformed (rejected by RE2 too).
      ErrorUnsupported, //!< Well-formed, but outside this layer's supported subset (see `error()`).
    };

    //! \brief Where a `Set` member (or the primitive `Match`, not exposed here) is anchored.
    enum class Anchor : std::uint8_t
    {
      UNANCHORED,   //!< Match anywhere in the text.
      ANCHOR_START, //!< Match must start at the beginning of the text.
      ANCHOR_BOTH,  //!< Match must span the entire text.
    };

    /*!
     * \brief `RE2::Set` — a set of patterns tested together. Mirrors real RE2's `Set`: buffer
     *        patterns with `Add`, `Compile` once, then `Match` repeatedly. Maps directly onto
     *        REAL's native `real::regex_set::which()` (index-list semantics match exactly).
     *
     * `Anchor` is synthesized by wrapping each added pattern (`^(?:…)`/`^(?:…)$`) before compiling,
     * since `real::regex_set` itself is natively unanchored-search only — documented here rather
     * than silently assumed.
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
          return -1;
        }
        std::string wrapped {anchor_wrap(pattern)};
        try {
          const real::regex probe(wrapped, RE2::options_to_flags(options_));
          (void) probe;
        } catch (const real::regex_error& e) {
          if (error != nullptr) {
            *error = e.what();
          }
          return -1;
        }
        const int index {static_cast<int>(patterns_.size())};
        patterns_.push_back(std::move(wrapped));
        return index;
      }

      /*!
       * \brief Compiles every buffered pattern into one `real::regex_set`.
       * \return `true` on success. `false` should not happen if every `Add` already succeeded
       *         (each pattern was already probe-compiled individually); kept for API fidelity.
       */
      bool Compile()
      {
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

      Options                             options_;  //!< Options shared by every member.
      Anchor                              anchor_;   //!< The anchor mode every member is matched with.
      std::vector<std::string>            patterns_; //!< Buffered, already anchor-wrapped member patterns.
      std::optional<real::regex_set>      set_;      //!< Engaged once `Compile` succeeds.
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
     * A separate overload from the `std::string_view` one below, not merely a call site of it:
     * `std::string`'s own conversion to `std::string_view` is itself user-defined, and C++ allows
     * at most one user-defined conversion in an implicit sequence, so a lone `string_view`
     * constructor would make `FullMatch(text, some_std_string, &arg)` stop compiling implicitly
     * (real RE2 keeps the same three-constructor split for the same reason).
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

    //! \brief Whether construction succeeded (`error_code() == ErrorCode::NoError`).
    [[nodiscard]] bool ok() const noexcept
    {
      return error_code_ == ErrorCode::NoError;
    }

    //! \brief The pattern text this `RE2` was built from.
    [[nodiscard]] const std::string& pattern() const noexcept
    {
      return pattern_;
    }

    //! \brief The rejection reason, or an empty string if `ok()`.
    [[nodiscard]] const std::string& error() const noexcept
    {
      return error_;
    }

    //! \brief The coarse rejection category, or `ErrorCode::NoError` if `ok()`.
    [[nodiscard]] ErrorCode error_code() const noexcept
    {
      return error_code_;
    }

    //! \brief The construction options this `RE2` was built with.
    [[nodiscard]] const Options& options() const noexcept
    {
      return options_;
    }

    //! \brief The number of capturing groups (excluding group 0), or `0` if `!ok()`.
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
      out.append(str->substr(0, match.start()));
      if (!expand_rewrite(out, rewrite, match)) {
        return false;
      }
      out.append(str->substr(match.end()));
      *str = std::move(out);
      return true;
    }

    /*!
     * \brief Replaces every non-overlapping match of \p re in `*str` with \p rewrite.
     *
     * \p rewrite may reference groups RE2-style: `\0` (whole match), `\1`…`\9`, `\\` for a literal
     * backslash.
     *
     * Empty-match policy matches real RE2's `GlobalReplace`: a zero-width match whose start
     * equals the end of the previous (accepted) match is skipped — it abuts the prior match and
     * must not produce a second rewrite (e.g. `a*` on `"aa"` yields one replacement, not two).
     * Legitimate non-abutting empty matches (e.g. `a*` on `"bbb"` → `#b#b#b#`) are still applied.
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
      std::string  out;
      out.reserve(str->size());
      std::size_t  last           {};
      int          count          {};
      bool         have_prev_end  {false};
      std::size_t  prev_end       {};
      const auto   matches        {re.longest_match_ ? re.regex_->find_iter_longest(*str) : re.regex_->find_iter(*str)};
      for (const auto& match : matches) {
        // RE2 empty-abut skip: do not rewrite a zero-width match that starts exactly where the
        // previous accepted match ended (nullable quantifier trailing empty after a greedy run).
        if (match.start() == match.end() && have_prev_end && match.start() == prev_end) {
          continue;
        }
        out.append(str->substr(last, match.start() - last));
        if (!expand_rewrite(out, rewrite, match)) {
          return 0;
        }
        last          = match.end();
        prev_end      = match.end();
        have_prev_end = true;
        ++count;
      }
      if (count == 0) {
        return 0;
      }
      out.append(str->substr(last));
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

    /*!
     * \brief Translates the `Options` fields this layer honors into `real::flags`.
     * \param[in] options The options to translate.
     * \return The equivalent `real::flags` set.
     */
    [[nodiscard]] static real::flags options_to_flags(const Options& options)
    {
      // allow_raw_byte, unconditionally: this layer's whole API is byte-offset C++ (mirroring RE2's
      // own), so \C landing mid-codepoint is exactly as safe here as it is under flags::bytes -- the
      // char-offset hazard the gate protects against (e.g. the Python str binding) never applies to
      // real::compat::re2. Widens the *gate* only; \C still always consumes exactly one raw byte.
      real::flags result {real::flags::allow_raw_byte};
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
        return;
      }
      longest_match_ = options.longest_match();
      const std::string effective {options.literal() ? QuoteMeta(pattern) : std::string(pattern)};
      try {
        regex_.emplace(effective, options_to_flags(options));
      } catch (const real::regex_error& e) {
        error_      = e.what();
        error_code_ = e.kind() == real::error_kind::unsupported ? ErrorCode::ErrorUnsupported
                                                                 : ErrorCode::ErrorSyntax;
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
} // namespace real::compat::re2

#endif // REAL_RE2_RE2_HPP
