/*!
 * \file regex_set.hpp
 * \brief `real::regex_set` — multi-pattern which-matched set.
 *
 * Which-matched semantics (RE2::Set / rust `RegexSet`): which members match the
 * subject at least once. **Not** \ref real::dfa munch (one winner at the cursor).
 *
 * Two search shapes. When enough patterns are DFA-eligible, a fused unanchored multi-accept DFA
 * (`dfa_mode::which_matched`) scans once for all of them: built at construction for a large set, or once a
 * mid-sized set has walked enough text to pay for it. Ineligible patterns (lookaround, a wide code-point
 * class such as text-mode \\w, …) and small sets walk one pattern at a time through `regex::search`. The
 * public bitset is always in **construction order**; fused rule indices are remapped.
 *
 * Include this header explicitly; \c real.hpp does not pull it in.
 */
#ifndef REAL_REGEX_SET_HPP
#define REAL_REGEX_SET_HPP

#include "real/dfa.hpp"
#include "real/real.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
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
     * \brief Eligible-member count at which the set builds its fused single-pass DFA at construction.
     *
     * One fused automaton costs a build and a full scan; N walks cost N searches that can each stop
     * early. From this count the fused scan pays on the first subject; between
     * \ref fused_deferred_min_eligible and here it pays only once enough text has been searched.
     */
    static constexpr std::size_t fused_min_eligible {56};

    /*!
     * \brief Eligible-member count from which a set builds its fused DFA once it has walked
     *        \ref fused_deferred_bytes, rather than at construction.
     *
     * From about this many members one fused scan costs less than the walks, but the build costs
     * milliseconds, more than a set built for one short subject gets back. So the set walks until its
     * whole-subject \ref matches calls total \ref fused_deferred_bytes, about the build's cost, which bounds
     * the total at about twice the cheaper choice in hindsight. Below this count the walks win outright.
     */
    static constexpr std::size_t fused_deferred_min_eligible {24};

    /*!
     * \brief Whole-subject bytes a set of \ref fused_deferred_min_eligible to \ref fused_min_eligible
     *        members walks before it builds its fused DFA.
     */
    static constexpr std::size_t fused_deferred_bytes {std::size_t {1} << 20U};


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
     * \brief True when a fused single-pass DFA is active: built at construction, or since, once the set
     *        walked \ref fused_deferred_bytes.
     * \return Whether the eligible members share one DFA rather than being searched individually.
     */
    [[nodiscard]] bool uses_fused() const noexcept
    {
      return ready_fused() != nullptr;
    }

    /*!
     * \brief How many members the fused DFA holds, or 0 when \ref uses_fused is false.
     *
     * Until the fused DFA is active every member walks individually, so this is 0 even
     * for patterns a DFA would accept.
     * \return The fused subset size, or 0.
     */
    [[nodiscard]] std::size_t eligible_count() const noexcept
    {
      const fused_state* st {ready_fused()};
      return st == nullptr ? 0U : st->eligible_orig.size();
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
      // Region or no fused path: pure N-walks (any-stop). An any-match walk stops at its first hit, so
      // is_match neither counts toward the deferred build nor triggers it.
      const fused_state* st {pos == 0 && endpos == npos ? ready_fused() : nullptr};
      if (st == nullptr) {
        // Unlike `matches`, the first served member is searched without the filter: had it matched, the
        // filter could not have excluded it, so scanning first is dead work exactly where an any-match walk
        // is fastest. The price is one extra member sweep when nothing matches.
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
      if (std::ranges::any_of(st->fused->which_matched(text), std::identity {})) {
        return true;
      }
      return std::ranges::any_of(st->ineligible_orig, [&](std::size_t oi) {
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
      const fused_state* st {pos == 0 && endpos == npos ? fused_for(text) : nullptr};
      if (st == nullptr) {
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
      const auto fbits {st->fused->which_matched(text)};
      for (std::size_t k = 0; k < fbits.size() && k < st->eligible_orig.size(); ++k) {
        if (fbits[k]) {
          hit[st->eligible_orig[k]] = true;
        }
      }
      // Ineligible: individual search (lookaround / klass_cp / …).
      for (const std::size_t oi : st->ineligible_orig) {
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
          // Name WHICH member: the reported position is an offset inside it, useless without the index,
          // which only this loop knows.
          throw regex_error(ex.cause() + " (in pattern " + std::to_string(i) + " of " +
                            std::to_string(patterns.size()) + ")",
                            ex.position(), ex.kind());
        }
      }
      if (members_.empty()) {
        return;
      }
      arm_byte_filter();
      // A set below the deferred threshold can never fuse (the eligible subset is at most the whole set):
      // skip the partition's per-member munch DFAs.
      if (members_.size() < fused_deferred_min_eligible) {
        return; // no fused state: every member uses N-walks
      }
      auto st {std::make_shared<fused_state>()};
      if (members_.size() >= fused_min_eligible) {
        // Large enough that the fused scan may pay on the first subject: partition now, and build now if
        // the eligible subset is large enough.
        partition(*st);
        if (st->eligible_orig.size() >= fused_min_eligible) {
          // A fused DFA past its bounds leaves the set on walks, as the deferred build does: a set of
          // patterns that each compile always builds.
          try {
            build_fused(*st);
          }
          catch (const dfa_error&) {
            st->fused.reset();
          }
          st->ready.store(true, std::memory_order_release);
        }
        else if (st->eligible_orig.size() < fused_deferred_min_eligible) {
          return; // too few eligibles ever to fuse
        }
      }
      fused_state_ = std::move(st);
    }

    /*!
     * \brief The fused scan's state: the DFA, its subset maps, and what the deferred build counts.
     *
     * Shared by copies of the set, and written once: at construction, or by the one call that builds it
     * under \ref once; \ref ready publishes it. Held by pointer because the atomics and the once flag can
     * be neither copied nor moved, and a set must stay both.
     */
    struct fused_state
    {
      std::optional<dfa>         fused;               //!< The fused DFA; empty until built, or when it cannot be.
      std::vector<std::size_t>   eligible_orig;       //!< fused rule k → construction index.
      std::vector<std::size_t>   ineligible_orig;     //!< construction indices needing search.
      bool                       partitioned {};      //!< The partition ran at construction.
      std::atomic<std::uint64_t> walked      {0};     //!< Whole-subject bytes \ref matches walked, until ready.
      std::atomic<bool>          ready       {false}; //!< The fields above are final (the DFA built or given up).
      std::once_flag             once;                //!< The deferred build runs once.
    };

    /*!
     * \brief Splits the members into the DFA-eligible subset and the rest (a single-pattern munch DFA
     *        decides eligibility).
     * \param[in,out] st The state receiving both lists.
     */
    void partition(fused_state& st) const
    {
      st.eligible_orig.reserve(members_.size());
      st.ineligible_orig.reserve(members_.size());
      for (std::size_t i = 0; i < members_.size(); ++i) {
        try {
          const real::dfa probe {std::span<const regex> {&members_[i], 1}};
          (void) probe;
          st.eligible_orig.push_back(i);
        }
        catch (const dfa_error&) {
          st.ineligible_orig.push_back(i);
        }
      }
      st.partitioned = true;
    }

    /*!
     * \brief Builds the fused which-matched DFA over the eligible members.
     * \param[in,out] st The partitioned state.
     * \throws dfa_error when the fused automaton exceeds its state or work bound.
     */
    void build_fused(fused_state& st) const
    {
      std::vector<regex> eligible_rx;
      eligible_rx.reserve(st.eligible_orig.size());
      for (const std::size_t i : st.eligible_orig) {
        eligible_rx.push_back(members_[i]);
      }
      st.fused.emplace(std::span<const regex> {eligible_rx}, dfa_mode::which_matched);
    }

    /*!
     * \brief The deferred build: partitions if construction did not, then builds the fused DFA when enough
     *        members are eligible. A fused DFA past its bounds leaves the set on walks for good, as does
     *        too small an eligible subset: \ref matches must not throw what construction did not.
     * \param[in,out] st The state to complete and publish.
     */
    void build_deferred(fused_state& st) const
    {
      if (!st.partitioned) {
        partition(st);
      }
      if (st.eligible_orig.size() >= fused_deferred_min_eligible) {
        try {
          build_fused(st);
        }
        catch (const dfa_error&) {
          st.fused.reset();
        }
      }
      st.ready.store(true, std::memory_order_release);
    }

    /*!
     * \brief The fused state when its DFA is built, without counting or building anything.
     * \return The state, or null while the set walks.
     */
    [[nodiscard]] const fused_state* ready_fused() const noexcept
    {
      const fused_state* st {fused_state_.get()};
      if (st == nullptr || !st->ready.load(std::memory_order_acquire) || !st->fused) {
        return nullptr;
      }
      return st;
    }

    /*!
     * \brief The fused state for a whole-subject \ref matches over \p text: counts \p text toward the
     *        deferred build, and runs the build once the count reaches \ref fused_deferred_bytes.
     * \param[in] text The subject about to be searched.
     * \return The state when its DFA is built, or null while the set walks.
     */
    [[nodiscard]] const fused_state* fused_for(std::string_view text) const
    {
      fused_state* st {fused_state_.get()};
      if (st == nullptr) {
        return nullptr;
      }
      if (!st->ready.load(std::memory_order_acquire)) {
        const std::uint64_t before {st->walked.fetch_add(text.size(), std::memory_order_relaxed)};
        if (before + text.size() < fused_deferred_bytes) {
          return nullptr;
        }
        std::call_once(st->once, [&] { build_deferred(*st); });
      }
      return st->fused ? st : nullptr;
    }

    /*!
     * \brief Widest first-byte union the byte filter carries: one 16-byte masked block's capacity.
     *
     * Private, unlike \ref fused_min_eligible, as a scan width rather than a contract; its rationale names
     * `detail::` symbols a public reference page cannot link.
     */
    static constexpr std::uint8_t filter_union_max {8};

    /*!
     * \brief Classifies members into a SPARSE partition one scan can serve, and arms the filter.
     *
     * Zero work per call is the constraint: classifying per query charges every call, including an any-match
     * walk whose first member hits at once. Everything happens here, once; the walks test one bit.
     *
     * The classification is the engine's own: `first_bytes_valid` plus `single_first` or a `small_set_size`
     * of 2..8. The hint builder sets `small_set` only when all 256 bytes of `first_bytes` count 2..8, so the
     * array is the COMPLETE leading set, which is what makes skipping a member sound. A nullable pattern
     * has `first_bytes_valid` false and stays wide.
     *
     * The union is capped in construction order (narrowest-first reorders which members are served, for no
     * gain), so several sparse members cannot form a dense union. A wide member never joins: one
     * `\w`-leading pattern costs the others nothing.
     */
    void arm_byte_filter()
    {
      if constexpr (!detail::have_members_scan) {
        // Not armed where `detail::have_members_scan` is false (x86-64, which still compiles and tests
        // `find_members`): the platform memchr is wider than this 128-bit scan, so member sweeps are cheaper.
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

    std::vector<regex>           members_;             //!< Every compiled pattern, in construction order.
    flags                        flags_ {flags::none}; //!< Flags shared by every member.
    std::shared_ptr<fused_state> fused_state_;         //!< Null when the set can never fuse.
    std::vector<std::uint64_t>   sparse_bits_;         //!< Bit i set when the byte filter serves member i.
    std::array<std::uint8_t, 8>  filter_bytes_ {};     //!< The served members' first-byte union, in the mask load's layout.
    std::uint8_t                 filter_count_ {0};    //!< Valid entries in \ref filter_bytes_; 0 = filter off.
  };
} // namespace real

#endif // REAL_REGEX_SET_HPP
