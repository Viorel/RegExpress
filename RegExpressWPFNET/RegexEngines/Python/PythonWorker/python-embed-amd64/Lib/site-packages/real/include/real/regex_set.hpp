/*!
 * \file regex_set.hpp
 * \brief `real::regex_set` — multi-pattern which-matched set.
 *
 * Which-matched semantics (RE2::Set / rust `RegexSet`): which members match the
 * subject at least once. **Not** \ref real::dfa munch (one winner at the cursor).
 *
 * **Stage-2 N-hybrid (internal):** when enough patterns are DFA-eligible, a fused
 * unanchored multi-accept DFA (`dfa_mode::which_matched`) scans once for those
 * members; ineligible patterns (lookaround, Unicode \\w/\\d/\\s, …) and small sets
 * use per-pattern `regex::search` (Stage-1). The public bitset is always in
 * **construction order** — fused rule indices are remapped via an eligible→orig map.
 *
 * Include this header explicitly; \c real.hpp does not pull it in.
 */
#ifndef REAL_REGEX_SET_HPP
#define REAL_REGEX_SET_HPP

#include "real/dfa.hpp"
#include "real/real.hpp"

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace real {

  /*!
   * \brief Multi-pattern set: which patterns match the subject at least once.
   *
   * Construction compiles every pattern (same \ref flags as \ref regex). If any
   * pattern is invalid or unsupported, the constructor throws \ref regex_error —
   * there is no silent skip. Capture groups are not reported by the set; re-run
   * the individual \ref regex if groups are needed.
   *
   * Bitset order is the construction order: index 0 is the first pattern, etc.
   */
  class regex_set
  {
  public:

    /*!
     * \brief Minimum DFA-eligible count to build a fused single-pass.
     *
     * Calibrated on log-like patterns (1 MiB corpus, M1, best-of-5): fused which_matched
     * beats Stage-1 N-walks for eligible.count ≥ ~56 (still N-walks-faster at 48).
     * Heuristic — re-measure with \c benchmarks/s2a_measure.cpp when retuning.
     */
    static constexpr std::size_t fused_min_eligible {56};

    /*!
     * \brief Compiles every pattern in \p patterns (construction order = bitset order).
     * \param[in] patterns      Pattern texts; an empty set is allowed.
     * \param[in] compile_flags Flags applied to every pattern (same as \ref regex).
     * \throws regex_error if any pattern fails to compile.
     */
    explicit regex_set(std::span<const std::string_view> patterns,
                       flags                             compile_flags = flags::none)
      : flags_(compile_flags)
    {
      build_from_views(patterns);
    }

    /*!
     * \brief Convenience: compile from a contiguous array of string views.
     * \param[in] patterns      Pointer to the first pattern.
     * \param[in] n             Pattern count.
     * \param[in] compile_flags Flags shared by every member.
     */
    regex_set(const std::string_view* patterns,
              std::size_t             n,
              flags                   compile_flags = flags::none)
      : regex_set(std::span<const std::string_view> {patterns, n},
                  compile_flags)
    {}

    /*!
     * \brief Brace-init: \c regex_set{"a", "b", R"(\\d+)"} .
     * \param[in] patterns      The patterns to compile.
     * \param[in] compile_flags Flags shared by every member.
     */
    regex_set(std::initializer_list<std::string_view> patterns,
              flags                                   compile_flags = flags::none)
      : regex_set(std::span<const std::string_view> {patterns.begin(), patterns.size()},
                  compile_flags)
    {}

    /*!
     * \brief Compile from owning strings (e.g. \c std::vector<std::string>).
     * \param[in] patterns      The patterns to compile; only borrowed for the duration of the call.
     * \param[in] compile_flags Flags shared by every member.
     */
    explicit regex_set(std::span<const std::string> patterns,
                       flags                        compile_flags = flags::none)
      : flags_(compile_flags)
    {
      std::vector<std::string_view> views;
      views.reserve(patterns.size());
      for (const std::string& pat : patterns) {
        views.emplace_back(pat);
      }
      build_from_views(views);
    }

    /*!
     * \brief Number of patterns in the set (bitset length).
     * \return The member count.
     */
    [[nodiscard]] std::size_t size() const noexcept
    {
      return members_.size();
    }

    /*!
     * \brief True if the set has no patterns.
     * \return Whether the set is empty.
     */
    [[nodiscard]] bool empty() const noexcept
    {
      return members_.empty();
    }

    /*!
     * \brief Compilation flags shared by every member.
     * \return The flag set every member was compiled with.
     */
    [[nodiscard]] flags compile_flags() const noexcept
    {
      return flags_;
    }

    /*!
     * \brief True when a fused single-pass DFA is active (eligible.count ≥ threshold).
     * \return Whether the eligible members share one DFA rather than being searched individually.
     */
    [[nodiscard]] bool uses_fused() const noexcept
    {
      return fused_.has_value();
    }

    /*!
     * \brief Size of the FUSED eligible subset — not an eligibility count in general.
     * \return How many members the fused DFA holds, or 0 when \ref uses_fused is false. Below the
     *         threshold every member walks individually, so the subset is discarded and this answers 0
     *         even for patterns the DFA would have accepted (pinned in test_regex_set_hybrid.cpp).
     */
    [[nodiscard]] std::size_t eligible_count() const noexcept
    {
      return eligible_orig_.size();
    }

    /*!
     * \brief True if **any** pattern matches the subject at least once.
     *
     * Stops at the first matching pattern (any-match early exit). Region
     * semantics match \ref regex::search — \p endpos truncates the view; \p pos
     * is the start offset (not a slice).
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return Whether at least one pattern matches.
     */
    [[nodiscard]] bool is_match(std::string_view text,
                                std::size_t      pos    = 0,
                                std::size_t      endpos = npos) const
    {
      // Region or no fused path: pure N-walks (any-stop).
      if (!fused_ || pos != 0 || endpos != npos) {
        for (const regex& re : members_) {
          if (re.search(text, pos, endpos)) {
            return true;
          }
        }
        return false;
      }
      // Fused path: any fused bit or any ineligible hit.
      for (bool b : fused_->which_matched(text)) {
        if (b) {
          return true;
        }
      }
      for (const std::size_t oi : ineligible_orig_) {
        if (members_[oi].search(text)) {
          return true;
        }
      }
      return false;
    }

    /*!
     * \brief Which patterns match at least once (construction-order bitset).
     *
     * Index \c i is true iff pattern \c i matched. Fused eligible bits are remapped
     * to original construction indices; ineligibles use per-pattern search.
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return One bool per member, in construction order.
     */
    [[nodiscard]] std::vector<bool> matches(std::string_view text,
                                            std::size_t      pos    = 0,
                                            std::size_t      endpos = npos) const
    {
      std::vector<bool> hit(members_.size(), false);
      if (members_.empty()) {
        return hit;
      }
      // Region: DFA which_matched is whole-subject mid-stream restart — use N-walks.
      if (!fused_ || pos != 0 || endpos != npos) {
        for (std::size_t i = 0; i < members_.size(); ++i) {
          if (members_[i].search(text, pos, endpos)) {
            hit[i] = true;
          }
        }
        return hit;
      }
      // Eligible: single-pass fused (indices 0..E-1) → map to construction order.
      const auto fbits {fused_->which_matched(text)};
      for (std::size_t k = 0; k < fbits.size() && k < eligible_orig_.size(); ++k) {
        if (fbits[k]) {
          hit[eligible_orig_[k]] = true;
        }
      }
      // Ineligible: individual search (lookaround / klass_cp / …).
      for (const std::size_t oi : ineligible_orig_) {
        if (members_[oi].search(text)) {
          hit[oi] = true;
        }
      }
      return hit;
    }

    /*!
     * \brief Indices of patterns that match (construction order, ascending).
     *
     * \param[in] text   Subject.
     * \param[in] pos    Byte offset the search starts at.
     * \param[in] endpos Byte offset the region ends at; defaults to the end of \p text.
     * \return The matching members' construction indices, ascending.
     */
    [[nodiscard]] std::vector<std::size_t> which(std::string_view text,
                                                 std::size_t      pos    = 0,
                                                 std::size_t      endpos = npos) const
    {
      std::vector<std::size_t> ids;
      const auto               hit {matches(text, pos, endpos)};
      ids.reserve(hit.size());
      for (std::size_t i = 0; i < hit.size(); ++i) {
        if (hit[i]) {
          ids.push_back(i);
        }
      }
      return ids;
    }

    /*!
     * \brief Access the compiled pattern at construction index \p i.
     * \param[in] i Construction index.
     * \return A reference to the compiled member.
     * \throws std::out_of_range when \p i is past the last member.
     */
    [[nodiscard]] const regex& operator[](std::size_t i) const
    {
      return members_.at(i);
    }

  private:

    /*!
     * \brief Compiles every pattern, then splits them into the DFA-eligible subset (fused when it
     *        reaches the threshold) and the ineligible remainder each query searches individually.
     * \param[in] patterns The patterns to compile, in construction order.
     * \throws regex_error if any pattern fails to compile.
     */
    void build_from_views(std::span<const std::string_view> patterns)
    {
      members_.reserve(patterns.size());
      for (const std::string_view pat : patterns) {
        members_.emplace_back(pat, flags_); // throws regex_error on failure
      }
      if (members_.empty()) {
        return;
      }
      // No set smaller than the threshold can ever reach it, because the eligible subset is at most
      // the whole set -- so the partition below cannot change the outcome, and every DFA it builds
      // would be discarded by the `else` branch at the end of this function. It was: a 40-pattern set
      // spent 447 us building and throwing away one full munch DFA per member before concluding what
      // its own size already said. Measured 11.2 us per pattern, all of it, for every set under 56.
      if (members_.size() < fused_min_eligible) {
        return; // eligible_orig_/ineligible_orig_ stay empty: every member uses N-walks
      }
      // Partition DFA-eligible vs ineligible (try single-pattern munch DFA).
      std::vector<regex> eligible_rx;
      eligible_rx.reserve(members_.size());
      eligible_orig_.reserve(members_.size());
      ineligible_orig_.reserve(members_.size());
      for (std::size_t i = 0; i < members_.size(); ++i) {
        try {
          const real::dfa probe {std::span<const regex> {&members_[i], 1}};
          (void) probe;
          eligible_rx.push_back(members_[i]);
          eligible_orig_.push_back(i);
        }
        catch (const dfa_error&) {
          ineligible_orig_.push_back(i);
        }
      }
      // Build fused only when enough eligibles to beat N-walks (calibrated threshold).
      if (eligible_rx.size() >= fused_min_eligible) {
        fused_.emplace(std::span<const regex> {eligible_rx}, dfa_mode::which_matched);
      }
      else {
        eligible_orig_.clear(); // fused inactive: all members use N-walks
        ineligible_orig_.clear();
      }
    }

    std::vector<regex>          members_;             //!< Every compiled pattern, in construction order.
    flags                       flags_ {flags::none}; //!< Flags shared by every member.
    std::optional<dfa>          fused_;               //!< Present when eligible ≥ threshold.
    std::vector<std::size_t>    eligible_orig_;       //!< fused rule k → construction index.
    std::vector<std::size_t>    ineligible_orig_;     //!< construction indices needing search.
  };
} // namespace real

#endif // REAL_REGEX_SET_HPP
