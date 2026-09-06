/*!
 * \file regex_set.hpp
 * \brief `real::regex_set` — multi-pattern which-matched set.
 *
 * Which-matched semantics (RE2::Set / rust `RegexSet`): which members match the
 * subject at least once. **Not** \ref real::dfa munch (one winner at the cursor).
 *
 * Two search shapes, chosen at construction. When enough patterns are DFA-eligible,
 * a fused unanchored multi-accept DFA (`dfa_mode::which_matched`) scans once for all
 * of them; ineligible patterns (lookaround, Unicode \\w/\\d/\\s, …) and sets below the
 * threshold walk one pattern at a time through `regex::search`. The public bitset is
 * always in **construction order** — fused rule indices are remapped through an
 * eligible→original map.
 *
 * Include this header explicitly; \c real.hpp does not pull it in.
 */
#ifndef REAL_REGEX_SET_HPP
#define REAL_REGEX_SET_HPP

#include "real/dfa.hpp"
#include "real/real.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
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
     * \brief Eligible-member count at which the set builds a fused single-pass DFA.
     *
     * A calibrated threshold: one fused automaton costs a build and a full
     * scan; N walks cost N searches that can each stop early. Below this
     * count every member is searched individually.
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
     * \brief How many members the fused DFA holds, or 0 when \ref uses_fused is false.
     *
     * Below the threshold every member walks individually, so this is 0 even
     * for patterns a DFA would have accepted.
     * \return The fused subset size, or 0.
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
        // THE FIRST SERVED MEMBER IS SEARCHED WITHOUT CONSULTING THE FILTER -- the whole difference
        // from `matches` below. If that member matches, the filter cannot have excluded it: a match
        // implies one of its own leading bytes is present in the region. So scanning first would be
        // dead work on exactly the case an any-match walk is fastest at, the subject where the first
        // member hits. The price is one extra member sweep when nothing matches at all.
        bool scanned {false};
        bool skip    {false};
        bool probed  {false};
        for (std::size_t i = 0; i < members_.size(); ++i) {
          if (is_sparse(i)) {
            if (!probed) {
              probed = true; // this one answers for itself; the filter serves the ones after it
            }
            else {
              if (!scanned) {
                skip    = filter_excludes(text, pos, endpos);
                scanned = true;
              }
              if (skip) {
                continue; // proven: no byte this member can start with occurs in the region
              }
            }
          }
          if (members_[i].search(text, pos, endpos)) {
            return true;
          }
        }
        return false;
      }
      // Fused path: any fused bit or any ineligible hit.
      if (std::ranges::any_of(fused_->which_matched(text), std::identity {})) {
        return true;
      }
      return std::ranges::any_of(ineligible_orig_, [&](std::size_t oi) {
                                   return members_[oi].search(text).matched();
                                 });
    }

    /*!
     * \brief Which patterns match at least once (construction-order bitset).
     *
     * Index \c i is true iff pattern \c i matched. Order is always construction
     * order, whether the set walks members individually or through a fused scan.
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
        // One scan for every member the filter serves; see is_match for why it is on demand.
        bool scanned {false};
        bool skip    {false};
        for (std::size_t i = 0; i < members_.size(); ++i) {
          if (is_sparse(i)) {
            if (!scanned) {
              skip    = filter_excludes(text, pos, endpos);
              scanned = true;
            }
            if (skip) {
              continue;
            }
          }
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
      for (std::size_t i = 0; i < patterns.size(); ++i) {
        try {
          members_.emplace_back(patterns[i], flags_);
        }
        catch (const regex_error& ex) {
          // WHICH member, said by the only place that knows. The position a member's own parser
          // reports is an offset INSIDE that member, so on its own it points into a string the
          // caller has to guess at: with three members and no index, a caller holding a generated
          // list has to re-compile them all to find out which one it was. The index is free here
          // and nowhere else.
          throw regex_error(ex.cause() + " (in pattern " + std::to_string(i) + " of " +
                            std::to_string(patterns.size()) + ")",
                            ex.position(), ex.kind());
        }
      }
      if (members_.empty()) {
        return;
      }
      arm_byte_filter();
      // No set smaller than the threshold can ever reach it: the eligible subset is at most the whole
      // set. Without this return the partition below would build one munch DFA per member and the
      // `else` branch at the end would throw every one of them away -- a per-pattern construction cost
      // paid by every small set to conclude what its own size already said.
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

    /*!
     * \brief Widest first-byte union the byte filter carries: one 16-byte masked block's capacity.
     *
     * PRIVATE, unlike \ref fused_min_eligible. That one is a contract a caller can reason about -- how many
     * eligible members it takes before a set fuses. This is the width of a scan, and nothing outside should
     * depend on it being eight. Keep it private for a second reason: its rationale names `detail::`
     * symbols, and a public brief that links to them has nothing to anchor on in the reference page.
     */
    static constexpr std::uint8_t filter_union_max {8};

    /*!
     * \brief Classifies members into a SPARSE partition one scan can serve, and arms the filter.
     *
     * ZERO WORK PER CALL is the design constraint, not an optimisation. Classifying inside the walk -- a
     * lookup per member per query -- charges every call, including the one an any-match walk is fastest
     * at: the set whose first member matches immediately. No scan primitive, however fast, recovers a
     * cost paid before it runs. Everything here happens once, in the constructor, and the walks below
     * test one bit.
     *
     * THE CLASSIFICATION IS THE ENGINE'S OWN, not a re-derivation. `first_bytes_valid` plus either
     * `single_first` or a `small_set_size` of 2..8 is exactly "this pattern has at most eight possible
     * leading bytes, enumerated". Soundness rests on the hint builder: it walks all 256 bytes of
     * `first_bytes` and sets `small_set` ONLY when the count lands in 2..8, so in that case the array is the
     * COMPLETE set rather than a sample -- which is what makes skipping a member safe. Past eight, neither
     * hint is set, so the member falls to the wide partition. A nullable pattern has `first_bytes_valid`
     * false and is wide with no special case.
     *
     * THE UNION IS CAPPED IN CONSTRUCTION ORDER. Taking narrowest-first looks better and is worse: it
     * reorders which members the filter serves for no gain. The cap is what stops several five-byte members
     * from combining into a twenty-byte union -- dense again, one level later, which is the very gate this
     * mechanism replaces. A member whose own leading set is wide never joins, so one `\w`-leading pattern
     * in a set costs the others nothing: it keeps being walked, the ones already in the union stay.
     */
    void arm_byte_filter()
    {
      if constexpr (!detail::have_members_scan) {
        // The scan EXISTS on x86-64 -- `find_members` is compiled under SSE2 and the tests exercise it
        // there. What does not exist is a reason to arm on it: the platform's own memchr is wider than
        // this 128-bit block scan, so the member sweeps it would replace are already cheaper than the
        // scan. See `detail::have_members_scan`. The walk stays as it is.
        return;
      }
      else {
        std::vector<char> uni;
        std::vector<bool> sparse(members_.size(), false);
        for (std::size_t i = 0; i < members_.size(); ++i) {
          const detail::pattern_hints& h {members_[i].raw_program().hints};
          if (!h.first_bytes_valid) {
            continue;
          }
          std::vector<char> mine;
          if (h.single_first >= 0) {
            mine.push_back(static_cast<char>(h.single_first));
          }
          else if (h.small_set_size >= 2U && h.small_set_size <= filter_union_max) {
            for (std::size_t k = 0; k < h.small_set_size; ++k) {
              mine.push_back(h.small_set[k]);
            }
          }
          else {
            continue; // more than eight possible leading bytes, or none enumerated
          }
          std::vector<char> merged {uni};
          for (const char byte : mine) {
            if (std::find(merged.begin(), merged.end(), byte) == merged.end()) {
              merged.push_back(byte);
            }
          }
          if (merged.size() > filter_union_max) {
            continue; // this one would not fit: it keeps being walked, the ones already in stay
          }
          uni       = std::move(merged);
          sparse[i] = true;
        }
        std::size_t served {0};
        for (std::size_t i = 0; i < members_.size(); ++i) {
          served += sparse[i] ? 1U : 0U;
        }
        if (served < 2U) {
          return; // one member gains nothing: its own search already prefilters on the same bytes
        }
        sparse_bits_.assign((members_.size() + 63U) / 64U, 0U);
        for (std::size_t i = 0; i < members_.size(); ++i) {
          if (sparse[i]) {
            sparse_bits_[i / 64U] |= (std::uint64_t {1} << (i % 64U));
          }
        }
        filter_count_ = static_cast<std::uint8_t>(uni.size());
        for (std::size_t k = 0; k < uni.size(); ++k) {
          filter_bytes_[k] = static_cast<std::uint8_t>(uni[k]);
        }
      }
    }

    /*!
     * \brief One bit, no search: is member \p i served by the byte filter?
     * \param[in] i Construction index of the member.
     * \return Whether the filter's union covers every byte this member can start with.
     */
    [[nodiscard]] bool is_sparse(std::size_t i) const noexcept
    {
      return filter_count_ != 0U && ((sparse_bits_[i / 64U] >> (i % 64U)) & std::uint64_t {1}) != 0U;
    }

    /*!
     * \brief True when the filter PROVES no member it serves can match in the region.
     * \param[in] text   Subject.
     * \param[in] pos    Start offset, as \ref regex::search takes it.
     * \param[in] endpos Region end, as \ref regex::search takes it (truncates the view).
     * \return Whether every served member can be skipped.
     */
    [[nodiscard]] bool filter_excludes(std::string_view text,
                                       std::size_t      pos,
                                       std::size_t      endpos) const
    {
      const std::size_t      end    {endpos < text.size() ? endpos : text.size()};
      const std::string_view region {text.substr(0, end)};
      return detail::find_members(region, pos, filter_bytes_, filter_count_) == npos;
    }

    std::vector<regex>          members_;             //!< Every compiled pattern, in construction order.
    flags                       flags_ {flags::none}; //!< Flags shared by every member.
    std::optional<dfa>          fused_;               //!< Present when eligible ≥ threshold.
    std::vector<std::size_t>    eligible_orig_;       //!< fused rule k → construction index.
    std::vector<std::size_t>    ineligible_orig_;     //!< construction indices needing search.
    std::vector<std::uint64_t>  sparse_bits_;         //!< Bit i set when the byte filter serves member i.
    std::array<std::uint8_t, 8> filter_bytes_ {};     //!< The served members' first-byte union, in the mask load's layout.
    std::uint8_t                filter_count_ {0};    //!< Valid entries in \ref filter_bytes_; 0 = filter off.
  };
} // namespace real

#endif // REAL_REGEX_SET_HPP
