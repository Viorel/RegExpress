/*!
 * \file pike.hpp
 * \brief The Pike VM — a Thompson NFA simulation — and its fast paths.
 *
 * Linear time in the input: every program counter is added to a list at most
 * once per position (generation-marked dedup), so no pattern can backtrack
 * catastrophically.
 *
 * The VM is generic over its container policy — `std::vector` for the
 * dynamic storage mode, fixed-capacity `static_vec` (storage.hpp) for
 * compile-time sized patterns, where a whole run performs zero heap
 * allocations.
 */
#ifndef REAL_PIKE_HPP
#define REAL_PIKE_HPP

#include "version.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "charclass.hpp"
#include "prefilter.hpp"
#include "program.hpp"

namespace real::detail {

  /*!
   * \brief How a VM run is anchored.
   */
  enum class run_mode : std::uint8_t
  {
    prefix, //!< Anchored at the start position (Python `re.match`).
    full,   //!< Anchored at both ends (Python `re.fullmatch`).
    search, //!< First match anywhere (Python `re.search`).
  };

  /*!
   * \brief One entry on the epsilon-closure DFS stack.
   *
   * Two kinds: explore a program counter (`pc >= 0`), or restore a
   * capture slot to its previous value once the subtree it covered is done
   * (`pc == -1`). This mutates one working slot array in place rather
   * than copying all slots per branch.
   */
  struct eps_entry
  {
    std::int32_t  pc;            //!< pc to explore, or -1 for a slot-restore entry.
    std::uint16_t slot;          //!< Slot to restore (restore entries).
    std::size_t   restore_value; //!< Value to restore the slot to.
  };

  /*!
   * \brief One priority-ordered list of NFA threads (leftmost-greedy semantics).
   *
   * `mark` is generation-stamped so clearing the list between positions is O(1).
   *
   * \tparam PcVec   Container of program counters.
   * \tparam SlotVec Flattened capture slots (pcs.size() * slot_count).
   * \tparam MarkVec Per-pc generation marks for O(1) dedup.
   */
  template <typename PcVec, typename SlotVec, typename MarkVec>
  struct basic_thread_list
  {
    PcVec         pcs;           //!< Live program counters, in priority order.
    SlotVec       slots;         //!< Flattened capture slots, parallel to \ref pcs.
    MarkVec       mark;          //!< Per-pc generation stamp (see \ref seen).
    std::uint64_t generation {}; //!< Current generation; bumped by \ref reset.

    /*!
     * \brief Clears the list in O(1) by bumping the generation.
     * \param[in] code_size Number of instructions (sizes the mark table once).
     */
    constexpr void reset(std::size_t code_size)
    {
      if (mark.size() != code_size) {
        mark.assign(code_size, 0);
        generation = 0;
      }
      ++generation;
      pcs.clear();
      slots.clear();
    }

    /*!
     * \brief Returns `true` if \p pc is already in this generation.
     * \param[in] pc A program counter.
     * \return `true` if \p pc is already in this generation.
     */
    [[nodiscard]] constexpr bool seen(std::int32_t pc) const
    {
      return mark[static_cast<std::size_t>(pc)] == generation;
    }

    /*!
     * \brief Marks \p pc as present in the current generation.
     * \param[in] pc The program counter.
     */
    constexpr void mark_seen(std::int32_t pc)
    {
      mark[static_cast<std::size_t>(pc)] = generation;
    }
  };

  /*!
   * \brief Reusable VM scratch state.
   *
   * One run allocates nothing once warm (and never allocates with static
   * containers); `find_all-style` loops reuse the same state across runs. The
   * two thread lists are flipped by index, never swapped.
   *
   * \tparam ThreadList The thread-list type (a \ref basic_thread_list).
   * \tparam WorkVec    Container for the working capture slots.
   * \tparam EpsVec     Container for the epsilon-closure stack.
   */
  template <typename ThreadList, typename WorkVec, typename EpsVec>
  struct basic_pike_state
  {
    ThreadList lists[2]; //!< Current and next thread lists (flipped by index).
    WorkVec    working;  //!< Capture slots along the current DFS path.
    EpsVec     stack;    //!< Epsilon-closure DFS stack.

    /*!
     * \brief Flat 256-byte membership table for the hot single-class scan, and
     *        the class index it was built for (-1 = none).
     *
     * The class-scanning fast paths (`[…]+`, `.`/negated-class) test one class
     * for every byte. A flat byte-indexed table answers membership with a single
     * load, versus the bitmap's shift-and-mask (measured ~2x faster in a tight
     * scan — the byte-classification technique used by DFA/JIT engines). It is
     * built once and reused across a `find_all`-style walk (the state is shared),
     * so it adds nothing to the program or to the static binary.
     */
    std::int32_t                   table_class {-1};
    std::array<std::uint8_t, 256>  table       {}; //!< 1 where the byte is in \ref table_class.
  };

  /*!
   * \brief Thread list specialized on `std::vector` (the dynamic storage mode).
   */
  using thread_list = basic_thread_list<std::vector<std::int32_t>, std::vector<std::size_t>, std::vector<std::uint64_t>>;
  /*!
   * \brief Reusable, isolated scratch for one level of lookaround evaluation (dynamic only).
   *
   * Vector-backed, independent of the main scratch's container policy; reset on each
   * evaluation, never sharing the main \ref basic_pike_state. One level suffices — nested
   * lookaround is rejected at compile time. Present only on the dynamic states; the static
   * state has no such member, so the lookaround code is `if constexpr`-elided there.
   */
  struct lookaround_scratch
  {
    thread_list            lists[2]; //!< Sub-VM thread lists (pcs only; the sub is capture-free).
    std::vector<eps_entry> stack;    //!< Sub-VM epsilon-closure stack.
  };

  /*!
   * \brief VM scratch state for the dynamic storage mode, plus the lookaround sub-scratch.
   */
  struct pike_state : basic_pike_state<thread_list, std::vector<std::size_t>, std::vector<eps_entry>>
  {
    lookaround_scratch lookaround; //!< Isolated sub-scratch for bounded lookaround evaluation.
  };

  /*!
   * \brief The Pike VM, generic over the scratch-state container policy.
   * \tparam State A \ref basic_pike_state instantiation (vector- or static-backed).
   */
  template <typename State>
  class pike_vm
  {
  public:

    /*!
     * \brief Binds the VM to a program and caller-owned scratch state.
     * \param[in]     prog  The compiled program to execute.
     * \param[in,out] state Reusable scratch (borrowed; must outlive the VM).
     */
    constexpr pike_vm(program_view prog,
                      State&       state)
      : prog_(prog),
        state_(state)
    {}

    /*!
     * \brief Runs the VM over \p text starting at \p start.
     *
     * On success fills \p out_slots with byte offsets (npos for unset capture
     * slots; slots 0/1 are the whole match).
     *
     * \tparam OutSlots Output slot container (resized to the program's slot count).
     * \param[in]  text               The subject text.
     * \param[in]  start              Index to begin matching/searching from.
     * \param[in]  mode               Anchoring mode (\ref run_mode).
     * \param[out] out_slots          Receives the capture slots on success.
     * \param[in]  forbid_empty_until Reject an empty match whose start is below
     *             this offset (the iterator sets it to the next codepoint
     *             boundary so a non-empty match may follow an empty one without
     *             re-yielding it — CPython 3.7+ rule). 0 means no restriction.
     * \return `true` if a match was found.
     */
    template <typename OutSlots>
    constexpr bool run(std::string_view text,
                       std::size_t      start,
                       run_mode         mode,
                       OutSlots&        out_slots,
                       std::size_t      forbid_empty_until = 0)
    {
      text_               = text;
      forbid_empty_until_ = forbid_empty_until;
      // Fast paths only fire for patterns that always consume (literal /
      // class+), which can never produce the empty match the flag guards.
      if (prog_.hints.greedy_class_loop >= 0) {
        return run_class_loop(text, start, mode, out_slots);
      }
      if (prog_.hints.exact_literal_len > 0) {
        return run_exact_literal(text, start, mode, out_slots);
      }
      if (prog_.hints.fixed_shape) {
        return run_fixed_shape(text, start, mode, out_slots);
      }
      if (prog_.hints.codepoint_class_ascii >= 0) {
        return run_codepoint_class(text, start, mode, out_slots);
      }
      if (prog_.hints.fixed_alternation) {
        return run_alternation(text, start, mode, out_slots);
      }
      const std::size_t code_size {prog_.code.size()};
      auto*             clist     {&state_.lists[0]};
      auto*             nlist     {&state_.lists[1]};
      clist->reset(code_size);
      nlist->reset(code_size);
      state_.working.assign(prog_.slot_count, npos);
      out_slots.assign(prog_.slot_count, npos);

      bool        matched {};
      std::size_t pos     {start};
      while (pos <= text.size()) {
        const bool seeding = (pos == start) || (mode == run_mode::search && !matched);
        if (seeding && mode == run_mode::search && !matched && clist->pcs.empty()) {
          // No thread is alive: jump straight to the next position
          // that could start a match (prefilter). Single pass, so the
          // linear-time guarantee is unaffected.
          pos = next_candidate(text, pos, start);
          if (pos > text.size()) {
            break; // no further start is possible (includes npos)
          }
          // Fresh generation before seeding at the jumped position: the list
          // may still carry `seen` marks from a previous position's epsilon
          // exploration whose threads all died, which would otherwise dedup
          // away (drop) the seed's own threads here.
          clist->reset(code_size);
        }
        if (seeding && seed_viable(text, pos, start)) {
          // A fresh thread must not inherit capture slots left in the
          // scratch by previously stepped threads.
          state_.working.assign(prog_.slot_count, npos);
          add_thread(*clist, 0, pos);
        }
        if (clist->pcs.empty()) {
          // The seed itself may die in the closure (failed assertion):
          // later positions must still be tried while searching. The
          // dead seed's seen-marks must not block the next one.
          if (matched || mode != run_mode::search || pos >= text.size()) {
            break;
          }
          clist->reset(code_size);
          ++pos;
          continue;
        }
        step(*clist, *nlist, pos, mode, matched, out_slots);
        auto* swap {clist};
        clist = nlist;
        nlist = swap;
        nlist->reset(code_size);
        ++pos;
      }
      return matched;
    }

  private:

    program_view     prog_;  //!< The program being executed.
    State&           state_; //!< Borrowed reusable scratch state.
    std::string_view text_;  //!< The subject text for the current run.

    /*!
     * \brief Reject empty matches whose start is below this offset.
     *
     * The CPython 3.7+ rule: after an empty match, the next match may not be
     * empty at the same spot, letting a non-empty match start there. The
     * iterator sets this to the next codepoint boundary so the skip stays
     * UTF-8 aligned. 0 means no restriction (single match/search/fullmatch
     * never restrict).
     */
    std::size_t forbid_empty_until_ {};

    /*!
     * \brief The concrete thread-list type taken from the bound `State`.
     */
    using list_type = std::remove_reference_t<decltype(std::declval<State&>().lists[0])>;

    /*!
     * \brief Returns a flat 256-byte membership table for class \p class_index.
     *
     * Materializes the class bitmap into a byte-indexed table the first time it
     * is requested, caching it in the shared scratch so a `find_all`-style walk
     * builds it once. In a tight per-byte scan, `table[b]` (one load) replaces
     * the bitmap's shift-and-mask — the byte-classification trick of DFA/JIT
     * engines, measured ~2x faster on the class-scanning fast paths.
     *
     * \param[in] class_index Index into the program's interned classes.
     * \return Pointer to a 256-entry table: 1 where the byte is in the class.
     */
    constexpr const std::uint8_t* class_table(std::size_t class_index)
    {
      if (state_.table_class != static_cast<std::int32_t>(class_index)) {
        const char_class& klass {prog_.classes[class_index]};
        for (std::size_t b {0}; b < 256; ++b) {
          state_.table[b] = klass.test(static_cast<std::uint8_t>(b)) ? 1U : 0U;
        }
        state_.table_class = static_cast<std::int32_t>(class_index);
      }
      return state_.table.data();
    }

    /*!
     * \brief Fast path for a whole-pattern "class+".
     *
     * Matches a maximal run of class bytes with one scan loop — exactly the
     * VM's greedy result, with no thread lists.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the (start, end) span on success.
     * \return `true` if a non-empty run was found.
     */
    template <typename OutSlots>
    constexpr bool run_class_loop(std::string_view text,
                                  std::size_t      start,
                                  run_mode         mode,
                                  OutSlots&        out_slots)
    {
      const std::uint8_t* const tbl =
        class_table(static_cast<std::size_t>(prog_.hints.greedy_class_loop));
      const auto in_class = [&](std::size_t i) {
                              return tbl[static_cast<std::uint8_t>(text[i])] != 0U;
                            };
      std::size_t match_start {start};
      if (mode == run_mode::search) {
        while (match_start < text.size() && !in_class(match_start)) {
          ++match_start;
        }
      }
      if (match_start >= text.size() || !in_class(match_start)) {
        out_slots.assign(2, npos);
        return false;
      }
      std::size_t match_end {match_start + 1};
      while (match_end < text.size() && in_class(match_end)) {
        ++match_end;
      }
      if (mode == run_mode::full && match_end != text.size()) {
        out_slots.assign(2, npos);
        return false;
      }
      out_slots.assign(2, npos);
      out_slots[0] = match_start;
      out_slots[1] = match_end;
      return true;
    }

    /*!
     * \brief Matches the run of byte/klass instructions starting at \p pc.
     *
     * Shared by the fixed-shape and alternation fast paths. Consumes one text
     * byte per instruction and stops at the first non-consuming op (a save,
     * jump or match).
     *
     * \param[in] text The subject text.
     * \param[in] pc   Index of the first instruction of the run.
     * \param[in] s    Text offset to match from.
     * \return The end offset on a full match, or \ref npos on a mismatch.
     */
    [[nodiscard]] constexpr std::size_t match_byte_klass_run(std::string_view text,
                                                             std::size_t      pc,
                                                             std::size_t      s) const
    {
      std::size_t consumed {};
      while (pc < prog_.code.size()) {
        const instr& instruction {prog_.code[pc]};
        if (instruction.op != opcode::byte && instruction.op != opcode::klass) {
          break;
        }
        if (s + consumed >= text.size()) {
          return npos;
        }
        const auto byte_value {static_cast<std::uint8_t>(text[s + consumed])};
        const bool ok         {instruction.op == opcode::byte ? byte_value == instruction.arg8
                                                   : prog_.classes[instruction.arg16].test(byte_value)};
        if (!ok) {
          return npos;
        }
        ++consumed;
        ++pc;
      }
      return s + consumed;
    }

    /*!
     * \brief Leftmost search by scanning candidate positions (first-byte hints).
     *
     * Shared by the fast paths that verify a fixed shape at a position: it walks
     * candidate starts via \ref next_candidate and reports the first that
     * \p match_at accepts.
     *
     * \tparam MatchAt  Callable `std::size_t(std::size_t pos)` returning the
     *                  match end at \p pos, or \ref npos.
     * \tparam OutSlots Output slot container (already sized to two).
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  match_at  The per-position matcher.
     * \param[out] out_slots Receives the (start, end) span on success.
     * \return `true` if a match was found.
     */
    template <typename MatchAt, typename OutSlots>
    constexpr bool fast_search(std::string_view text,
                               std::size_t      start,
                               MatchAt          match_at,
                               OutSlots&        out_slots)
    {
      std::size_t match_start {start};
      while (match_start <= text.size()) {
        match_start = next_candidate(text, match_start, start);
        if (match_start > text.size()) {
          break;
        }
        const std::size_t match_end {match_at(match_start)};
        if (match_end != npos) {
          out_slots[0] = match_start;
          out_slots[1] = match_end;
          return true;
        }
        ++match_start;
      }
      return false;
    }

    /*!
     * \brief Fast path for a whole-pattern fixed-width byte/klass sequence.
     *
     * A straight-line program (no branches/assertions) has exactly one thread,
     * so a match is a fixed-width sequence verified by a single walk: each
     * `byte`/`klass` instruction consumes one text byte. There is no greedy/lazy
     * ambiguity. Covers `class{n}` and mixed shapes like `\d{4}-\d{2}-\d{2}`.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if the sequence matched.
     */
    template <typename OutSlots>
    constexpr bool run_fixed_shape(std::string_view text,
                                   std::size_t      start,
                                   run_mode         mode,
                                   OutSlots&        out_slots)
    {
      out_slots.assign(2, npos);
      // The sequence starts after the single leading save (the detection in
      // analyze_program requires exactly one) and runs to save 1.
      const auto at = [&](std::size_t s) { return match_byte_klass_run(text, 1, s); };

      if (mode != run_mode::search) {
        const std::size_t match_end {at(start)};
        if (match_end == npos || (mode == run_mode::full && match_end != text.size())) {
          return false;
        }
        out_slots[0] = start;
        out_slots[1] = match_end;
        return true;
      }
      return fast_search(text, start, at, out_slots);
    }

    /*!
     * \brief Fast path for `.` / a negated class, optionally a greedy `+`.
     *
     * Scans codepoints directly, mirroring the byte-level expansion the VM would
     * run: an ASCII byte matches the ASCII set; a valid 2–4 byte UTF-8 sequence
     * always matches (a negated ASCII class excludes only ASCII); anything else
     * (lone continuation, bad lead, truncation) stops, exactly as the VM's
     * lead/continuation branches would fail. Covers `.+`, `[^,]+`, `.`, `[^,]`.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if at least one codepoint matched.
     */
    template <typename OutSlots>
    constexpr bool run_codepoint_class(std::string_view text,
                                       std::size_t      start,
                                       run_mode         mode,
                                       OutSlots&        out_slots)
    {
      const std::uint8_t* const ascii {
        class_table(static_cast<std::size_t>(prog_.hints.codepoint_class_ascii))};
      out_slots.assign(2, npos);

      const auto cont = [&](std::size_t i) {
                          const auto cont_byte {static_cast<std::uint8_t>(text[i])};
                          return cont_byte >= 0x80 && cont_byte <= 0xBF;
                        };
      // Byte length of a matching codepoint at i, or 0 for no match.
      const auto width = [&](std::size_t i) -> std::size_t {
                           const auto byte_value {static_cast<std::uint8_t>(text[i])};
                           if (byte_value < 0x80) {
                             return ascii[byte_value] != 0U ? 1 : 0;
                           }
                           if (byte_value >= 0xC2 && byte_value <= 0xDF) {
                             return i + 1 < text.size() && cont(i + 1) ? 2 : 0;
                           }
                           if (byte_value >= 0xE0 && byte_value <= 0xEF) {
                             return i + 2 < text.size() && cont(i + 1) && cont(i + 2) ? 3 : 0;
                           }
                           if (byte_value >= 0xF0 && byte_value <= 0xF4) {
                             return i + 3 < text.size() && cont(i + 1) && cont(i + 2) && cont(i + 3) ? 4 : 0;
                           }
                           return 0;
                         };

      std::size_t match_start {start};
      if (mode == run_mode::search) {
        while (match_start < text.size() && width(match_start) == 0) {
          ++match_start;
        }
      }
      if (match_start >= text.size()) {
        return false;
      }
      const std::size_t first_width {width(match_start)};
      if (first_width == 0) {
        return false;
      }
      std::size_t match_end {match_start + first_width};
      if (prog_.hints.codepoint_class_plus) {
        while (match_end < text.size()) {
          const std::size_t codepoint_width {width(match_end)};
          if (codepoint_width == 0) {
            break;
          }
          match_end += codepoint_width;
        }
      }
      if (mode == run_mode::full && match_end != text.size()) {
        return false;
      }
      out_slots[0] = match_start;
      out_slots[1] = match_end;
      return true;
    }

    /*!
     * \brief Fast path for an alternation of straight-line branches.
     *
     * Each branch is a fixed-width byte/klass sequence, so at a candidate the
     * branches are tried in source order (leftmost-first priority) and the first
     * that matches wins — exactly the Pike VM's thread priority. The branch
     * structure is read directly from the split chain in the program.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the matched span on success.
     * \return `true` if some branch matched.
     */
    template <typename OutSlots>
    constexpr bool run_alternation(std::string_view text,
                                   std::size_t      start,
                                   run_mode         mode,
                                   OutSlots&        out_slots)
    {
      out_slots.assign(2, npos);
      const auto& code {prog_.code};

      // First branch that matches at \p s (and, for full, spans to the end). The
      // branches are read from the split chain in source order (highest priority
      // first), mirroring the VM's thread priority.
      const auto match_at = [&](std::size_t match_start, bool require_full) -> std::size_t {
                              std::size_t pc {1};
                              while (true) {
                                const bool        is_split  {code[pc].op == opcode::split};
                                const std::size_t branch    {is_split ? static_cast<std::size_t>(code[pc].primary_target) : pc};
                                const std::size_t match_end {match_byte_klass_run(text, branch, match_start)};
                                if (match_end != npos && (!require_full || match_end == text.size())) {
                                  return match_end;
                                }
                                if (!is_split) {
                                  return npos;
                                }
                                pc = static_cast<std::size_t>(code[pc].secondary_target);
                              }
                            };

      if (mode != run_mode::search) {
        const std::size_t match_end {match_at(start, mode == run_mode::full)};
        if (match_end == npos) {
          return false;
        }
        out_slots[0] = start;
        out_slots[1] = match_end;
        return true;
      }
      return fast_search(text, start, [&](std::size_t match_start) { return match_at(match_start, false); }, out_slots);
    }

    /*!
     * \brief Tests whether the fixed literal prefix occurs at \p cand.
     * \param[in] text The subject text.
     * \param[in] cand Candidate start offset.
     * \param[in] len  Length of the literal (`hints.exact_literal_len`).
     * \return `true` if `text[cand : cand+len]` equals the literal.
     */
    [[nodiscard]] constexpr bool literal_at(std::string_view text,
                                            std::size_t      cand,
                                            std::size_t      len) const
    {
      if (cand + len > text.size()) {
        return false;
      }
      const auto pfx {std::string_view(prog_.hints.prefix.data(), len)};
      if (std::is_constant_evaluated()) {
        return text.substr(cand, len) == pfx;
      }
      return std::memcmp(text.data() + cand, pfx.data(), len) == 0;
    }

    /*!
     * \brief Fills capture slots for a literal match at \p cand.
     *
     * Replays `save` instructions at their consumed offsets and checks any
     * zero-width assertions in the chain at \p cand.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  cand      Start offset of the literal match.
     * \param[in]  len       Length of the literal.
     * \param[out] out_slots Receives the capture slots.
     * \return `false` (and clears \p out_slots) if an assertion fails here, so
     *         the caller tries the next occurrence; `true` otherwise.
     */
    template <typename OutSlots>
    constexpr bool replay_literal(std::size_t cand,
                                  std::size_t len,
                                  OutSlots&   out_slots) const
    {
      out_slots.assign(prog_.slot_count, npos);
      std::size_t consumed {};
      for (std::size_t pc = 0; pc < prog_.code.size(); ++pc) {
        const instr& instruction {prog_.code[pc]};
        if (instruction.op == opcode::save) {
          out_slots[instruction.arg16] = cand + consumed;
        }
        else if (instruction.op == opcode::assert_position) {
          if (!assertion_holds(static_cast<assert_kind>(instruction.arg8), cand + consumed)) {
            out_slots.assign(prog_.slot_count, npos);
            return false;
          }
        }
        else if ((instruction.op == opcode::byte || instruction.op == opcode::klass) && consumed < len) {
          ++consumed;
        }
        else if (instruction.op == opcode::match) {
          break;
        }
      }
      if (prog_.slot_count >= 2 && out_slots[1] == npos) {
        out_slots[1] = cand + len; // group 0 end, even if replay ended early
      }
      return true;
    }

    /*!
     * \brief Fast path for a pure-literal pattern.
     *
     * The prefilter locates the fixed bytes; this replays saves directly, with
     * no thread lists, epsilon stack or per-position stepping. A leading or
     * trailing zero-width assertion (`\b`, `^`, `$` …) may make a given
     * occurrence fail, so in search mode it scans successive occurrences until
     * the assertions hold — the case a differential-fuzz finding (`\B2` on
     * `"220"`) exposed.
     *
     * \tparam OutSlots Output slot container.
     * \param[in]  text      The subject text.
     * \param[in]  start     Index to begin at.
     * \param[in]  mode      Anchoring mode.
     * \param[out] out_slots Receives the capture slots on success.
     * \return `true` if a match was found.
     */
    template <typename OutSlots>
    constexpr bool run_exact_literal(std::string_view text,
                                     std::size_t      start,
                                     run_mode         mode,
                                     OutSlots&        out_slots)
    {
      const std::size_t len {static_cast<std::size_t>(prog_.hints.exact_literal_len)};
      if (len == 0) {
        out_slots.assign(prog_.slot_count, npos);
        return false;
      }
      if (mode != run_mode::search) {
        const bool full_ok = mode != run_mode::full || start + len == text.size();
        const bool ok      = literal_at(text, start, len) && full_ok &&
                             replay_literal(start, len, out_slots);
        if (!ok) {
          out_slots.assign(prog_.slot_count, npos);
        }
        return ok;
      }
      std::size_t from {start};
      while (true) {
        const std::size_t cand {next_candidate(text, from, start)};
        if (cand > text.size() || cand + len > text.size()) {
          out_slots.assign(prog_.slot_count, npos);
          return false;
        }
        if (literal_at(text, cand, len) && replay_literal(cand, len, out_slots)) {
          return true;
        }
        from = cand + 1; // assertion failed here; try the next occurrence
      }
    }

    /*!
     * \brief First position >= \p pos that could start a match, per the hints.
     *
     * The prefilter step: jumps over positions that provably cannot start a
     * match (literal prefix search, unique first byte, line start, first-byte
     * set). Returns \p pos itself when no skipping applies.
     *
     * \param[in] text  The subject text.
     * \param[in] pos   Current position.
     * \param[in] start The run's start offset (for one-shot anchored patterns).
     * \return The next candidate offset, or \ref real::npos if none exists.
     */
    [[nodiscard]] constexpr std::size_t next_candidate(std::string_view text,
                                                       std::size_t      pos,
                                                       std::size_t      start) const
    {
      const pattern_hints& hints {prog_.hints};
      if (hints.anchored_start) {
        return pos == start ? pos : npos; // one shot at the start
      }
      if (hints.prefix_size >= 2) {
        return find_prefix(text, pos, std::string_view(hints.prefix.data(), hints.prefix_size));
      }
      if (hints.single_first >= 0) {
        return find_byte(text, pos, static_cast<char>(hints.single_first));
      }
      if (hints.line_anchored && pos != start) {
        const std::size_t nl {find_byte(text, pos - 1, '\n')};
        return nl == npos ? npos : nl + 1;
      }
      if (hints.first_bytes_valid) {
        while (pos < text.size() &&
               !hints.first_bytes.test(static_cast<std::uint8_t>(text[pos]))) {
          ++pos;
        }
        return pos < text.size() ? pos : npos;
      }
      return pos;
    }

    /*!
     * \brief Cheap pre-check before seeding a new thread at \p pos.
     *
     * Live threads may force the loop through positions the prefilter would
     * have skipped; this avoids seeding where a match cannot start. It also
     * enforces codepoint alignment: in non-byte mode a UTF-8 continuation byte
     * is never a valid match start.
     *
     * \param[in] text  The subject text.
     * \param[in] pos   The candidate seed position.
     * \param[in] start The run's start offset.
     * \return `true` if a fresh thread should be seeded at \p pos.
     */
    [[nodiscard]] constexpr bool seed_viable(std::string_view text,
                                             std::size_t      pos,
                                             std::size_t      start) const
    {
      const pattern_hints& hints {prog_.hints};
      if (hints.anchored_start && pos != start) {
        return false;
      }
      // A match can never start inside a multi-byte codepoint: in non-byte mode
      // a UTF-8 continuation byte (10xxxxxx) is not a valid start position. This
      // keeps zero-width matches (\b, \B, ^, $, empty) codepoint-aligned, like a
      // codepoint-based engine — bytes mode seeds every byte.
      if (!prog_.byte_mode && pos < text.size() &&
          (static_cast<std::uint8_t>(text[pos]) & 0xC0U) == 0x80U) {
        return false;
      }
      if (!hints.first_bytes_valid) {
        return true;
      }
      return pos < text.size() && hints.first_bytes.test(static_cast<std::uint8_t>(text[pos]));
    }

    /*!
     * \brief Evaluates a zero-width assertion at \p pos in the current text.
     * \param[in] kind The assertion to evaluate.
     * \param[in] pos  The position at which to evaluate it.
     * \return `true` if the assertion holds there.
     */
    [[nodiscard]] constexpr bool assertion_holds(assert_kind kind,
                                                 std::size_t pos) const
    {
      const std::size_t len {text_.size()};
      const auto        byte_at = [&](std::size_t i) { return static_cast<std::uint8_t>(text_[i]); };
      bool              result {};
      switch (kind) {
        case assert_kind::text_start:
          result = pos == 0;
          break;
        case assert_kind::text_end:
          result = pos == len;
          break;
        case assert_kind::text_end_or_final_newline:
          result = pos == len || (pos + 1 == len && byte_at(pos) == '\n');
          break;
        case assert_kind::line_start:
          result = pos == 0 || byte_at(pos - 1) == '\n';
          break;
        case assert_kind::line_end:
          result = pos == len || byte_at(pos) == '\n';
          break;
        case assert_kind::word_boundary:
        case assert_kind::not_word_boundary:
          {
            const bool before {pos > 0 && is_ascii_word_byte(byte_at(pos - 1))};
            const bool after  {pos < len && is_ascii_word_byte(byte_at(pos))};
            result = (before != after) == (kind == assert_kind::word_boundary);
          }
          break;
        case assert_kind::word_start:
        case assert_kind::word_end:
          {
            const bool before {pos > 0 && is_ascii_word_byte(byte_at(pos - 1))};
            const bool after  {pos < len && is_ascii_word_byte(byte_at(pos))};
            result = kind == assert_kind::word_start ? (!before && after) : (before && !after);
          }
          break;
      }
      return result;
    }

    /*!
     * \brief Advances every thread of \p clist by the byte at \p pos.
     *
     * Survivors that consumed a byte land in \p nlist. A thread reaching
     * `match` records its slots and cuts all lower-priority threads, so
     * priority (leftmost-greedy) order is preserved.
     *
     * \tparam OutSlots Output slot container.
     * \param[in,out] clist     The current thread list (consumed).
     * \param[in,out] nlist     The next thread list (receives survivors).
     * \param[in]     pos        The current input position.
     * \param[in]     mode       Anchoring mode (affects `match` acceptance).
     * \param[in,out] matched    Set to `true` when a match is recorded.
     * \param[out]    out_slots  Receives the slots of an accepted match.
     */
    template <typename OutSlots>
    constexpr void step(list_type&  clist,
                        list_type&  nlist,
                        std::size_t pos,
                        run_mode    mode,
                        bool&       matched,
                        OutSlots&   out_slots)
    {
      const std::uint16_t slot_count {prog_.slot_count};
      for (std::size_t i = 0; i < clist.pcs.size(); ++i) {
        const std::int32_t pc {clist.pcs[i]};
        const instr&       instruction {prog_.code[static_cast<std::size_t>(pc)]};
        const std::size_t  base {i * slot_count};
        switch (instruction.op) {
          case opcode::byte:
            if (pos < text_.size() &&
                static_cast<std::uint8_t>(text_[pos]) == instruction.arg8) {
              load_working(clist, base);
              add_thread(nlist, pc + 1, pos + 1);
            }
            break;
          case opcode::klass:
            if (pos < text_.size() &&
                prog_.classes[instruction.arg16].test(static_cast<std::uint8_t>(text_[pos]))) {
              load_working(clist, base);
              add_thread(nlist, pc + 1, pos + 1);
            }
            break;
          case opcode::match:
            if (mode == run_mode::full && pos != text_.size()) {
              break; // must consume the whole text: thread dies
            }
            // Reject an empty match forbidden at this position; a lower-priority
            // thread may still consume a byte and win a non-empty match here.
            if (pos == clist.slots[base] && clist.slots[base] < forbid_empty_until_) {
              break;
            }
            for (std::uint16_t s = 0; s < slot_count; ++s) {
              out_slots[s] = clist.slots[base + s];
            }
            matched = true;
            return; // drop lower-priority threads
          case opcode::split:
          case opcode::jump:
          case opcode::save:
          case opcode::assert_position:
          case opcode::assert_lookaround:
            break; // epsilon ops never appear in a stepped list
        }
      }
    }

    /*!
     * \brief Loads a thread's saved slots into the working slot array.
     * \param[in] clist The list holding the thread.
     * \param[in] base  Flattened offset of the thread's slots in `clist.slots`.
     */
    constexpr void load_working(const list_type& clist,
                                std::size_t      base)
    {
      for (std::uint16_t s = 0; s < prog_.slot_count; ++s) {
        state_.working[s] = clist.slots[base + s];
      }
    }

    /*!
     * \brief Adds \p pc0 and its whole epsilon closure to \p list.
     *
     * Threads are added in DFS (priority) order; the current `working` slots
     * are snapshotted into the list for each consuming thread. Saves and
     * assertions are handled during the closure walk.
     *
     * \param[in,out] list The thread list to populate.
     * \param[in]     pc0  The program counter to seed from.
     * \param[in]     pos  The current input position (for `save` / assertions).
     */
    constexpr void add_thread(list_type&   list,
                              std::int32_t pc0,
                              std::size_t  pos)
    {
      auto& stack {state_.stack};
      stack.clear();
      stack.push_back({.pc = pc0, .slot = 0, .restore_value = 0});
      while (!stack.empty()) {
        const auto entry {stack.back()};
        stack.pop_back();
        if (entry.pc < 0) {
          state_.working[entry.slot] = entry.restore_value;
          continue;
        }
        const std::int32_t pc {entry.pc};
        if (list.seen(pc)) {
          continue;
        }
        list.mark_seen(pc);
        const instr& instruction {prog_.code[static_cast<std::size_t>(pc)]};
        switch (instruction.op) {
          case opcode::jump:
            stack.push_back({.pc = instruction.primary_target, .slot = 0, .restore_value = 0});
            break;
          case opcode::split:
            // primary_target is preferred: push secondary first so primary pops (explores) first.
            stack.push_back({.pc = instruction.secondary_target, .slot = 0, .restore_value = 0});
            stack.push_back({.pc = instruction.primary_target, .slot = 0, .restore_value = 0});
            break;
          case opcode::save:
            stack.push_back({.pc              = -1,
                             .slot            = instruction.arg16,
                             .restore_value   = state_.working[instruction.arg16]});
            state_.working[instruction.arg16] = pos;
            stack.push_back({.pc              = pc + 1, .slot = 0, .restore_value = 0});
            break;
          case opcode::assert_position:
            if (assertion_holds(static_cast<assert_kind>(instruction.arg8), pos)) {
              stack.push_back({.pc = pc + 1, .slot = 0, .restore_value = 0});
            }
            break;
          case opcode::assert_lookaround:
            // Epsilon: the thread proceeds only if the bounded sub-pattern holds here,
            // evaluated on the isolated sub-scratch. Dynamic only — the static state has
            // no `lookaround` member and static_regex rejects lookarounds at compile, so
            // this is `if constexpr`-elided (zero footprint for static_regex).
            // (intentionally uncovered: llvm-cov reports 0 on the `if constexpr`-false
            // instantiations; the dynamic instantiation that runs lookaround_holds is covered.)
            if constexpr (requires(State & s) {
            s.lookaround;
          }) {
              if (lookaround_holds(instruction.arg16, pos)) {
                stack.push_back({.pc = pc + 1, .slot = 0, .restore_value = 0});
              }
            }
            break;
          case opcode::byte:
          case opcode::klass:
          case opcode::match:
            list.pcs.push_back(pc);
            for (std::uint16_t s = 0; s < prog_.slot_count; ++s) {
              list.slots.push_back(state_.working[s]);
            }
            break;
        }
      }
    }

    /*!
     * \brief Evaluates a bounded lookaround at \p pos (true if the thread should proceed).
     *
     * Dispatches on direction and applies the negation. Both directions run a self-contained
     * Pike simulation of the sub-program region on a DEDICATED, isolated sub-scratch
     * (`state_.lookaround`) — the main `state_` (lists/working/stack) is never touched, so an
     * in-flight match is unaffected (the isolation invariant) — and are bounded to `l_max`
     * bytes (the source of strict linearity per position). The sub is capture-free; `(?!` /
     * `(?<!` negate the result.
     *
     * \param[in] sub_id Index into `prog_.lookarounds`.
     * \param[in] pos    The text position the assertion is evaluated at.
     * \return `true` if the (possibly negated) assertion holds, so the thread proceeds.
     */
    [[nodiscard]] constexpr bool lookaround_holds(std::uint16_t sub_id,
                                                  std::size_t   pos)
    {
      const lookaround_sub& sub     {prog_.lookarounds[sub_id]};
      const bool            matched {sub.direction == look_dir::behind
                                       ? lookbehind_matches(sub, pos)
                                       : lookahead_matches(sub, pos)};
      return sub.negative ? !matched : matched;
    }

    /*!
     * \brief Lookahead: does the sub-pattern match a prefix starting at \p pos?
     *
     * Forward Pike simulation from \p pos, bounded to `l_max` bytes, stopping at the first
     * `match` (the sub is capture-free, so any reached `match` is a witness).
     */
    [[nodiscard]] constexpr bool lookahead_matches(const lookaround_sub& sub,
                                                   std::size_t           pos)
    {
      const std::size_t code_size {prog_.code.size()};
      thread_list*      clist     {&state_.lookaround.lists[0]};
      thread_list*      nlist     {&state_.lookaround.lists[1]};
      clist->reset(code_size);
      nlist->reset(code_size);
      bool matched            {};
      sub_add_thread(*clist, sub.code_offset, pos, matched);
      const std::size_t limit {pos + static_cast<std::size_t>(sub.l_max)};
      for (std::size_t p {pos}; !matched && !clist->pcs.empty() && p < text_.size() && p < limit; ++p) {
        const auto byte_value {static_cast<std::uint8_t>(text_[p])};
        for (const std::int32_t pc : clist->pcs) {
          const instr& in      {prog_.code[static_cast<std::size_t>(pc)]};
          // Parked pcs are only consuming ops; the ternary's else assumes klass (sub_add_thread
          // parks nothing else). The assert makes that invariant explicit (no-op under NDEBUG).
          assert((in.op == opcode::byte || in.op == opcode::klass) && "lookaround parked a non-consuming op");
          const bool   consume {in.op == opcode::byte ? byte_value == in.arg8
                                                      : prog_.classes[in.arg16].test(byte_value)};
          if (consume) {
            sub_add_thread(*nlist, pc + 1, p + 1, matched);
          }
        }
        thread_list* const done {clist};
        clist = nlist;
        nlist = done;
        nlist->reset(code_size);
      }
      return matched;
    }

    /*!
     * \brief Lookbehind: does the sub-pattern match a window ENDING EXACTLY at \p pos?
     *
     * The match must finish precisely at \p pos, not merely somewhere inside the window —
     * the defining correctness trap of lookbehind. Candidate starts run from \p pos backward
     * to `pos - l_max` (bytes, A1); in non-bytes mode a start may not fall on a UTF-8
     * continuation byte, which would split a codepoint (A9). The first start whose sub-pattern
     * fullmatches `[s, pos)` is a witness.
     */
    [[nodiscard]] constexpr bool lookbehind_matches(const lookaround_sub& sub,
                                                    std::size_t           pos)
    {
      const std::size_t lmax         {static_cast<std::size_t>(sub.l_max)};
      const std::size_t window_start {pos > lmax ? pos - lmax : 0};
      for (std::size_t s {pos};; --s) {
        // s == pos reads nothing (always a valid boundary); for s < pos the start must begin
        // a codepoint — not a 0x80–0xBF continuation byte — unless we are in raw-bytes mode.
        const bool aligned {prog_.byte_mode || s >= pos
                            || (static_cast<std::uint8_t>(text_[s]) & 0xC0U) != 0x80U};
        if (aligned && sub_fullmatch_window(sub.code_offset, s, pos)) {
          return true;
        }
        if (s == window_start) {
          break; // reached the far edge; stop before s underflows past 0
        }
      }
      return false;
    }

    /*!
     * \brief Reports whether the sub-program, run from \p start, reaches `match` EXACTLY at
     *        \p pos (a fullmatch of `[start, pos)`), on the isolated sub-scratch.
     *
     * A `match` reached before \p pos (a shorter window) is deliberately discarded — lookbehind
     * requires the sub to end at \p pos. Touches only `state_.lookaround`.
     */
    [[nodiscard]] constexpr bool sub_fullmatch_window(std::int32_t code_offset,
                                                      std::size_t  start,
                                                      std::size_t  pos)
    {
      const std::size_t code_size {prog_.code.size()};
      thread_list*      clist     {&state_.lookaround.lists[0]};
      thread_list*      nlist     {&state_.lookaround.lists[1]};
      clist->reset(code_size);
      nlist->reset(code_size);
      bool here {false};
      sub_add_thread(*clist, code_offset, start, here);
      if (start == pos) {
        return here; // empty window: the sub must match the empty string exactly at pos
      }
      bool sink {false}; // matches reached before pos: collected then ignored
      for (std::size_t p {start}; p < pos; ++p) {
        if (clist->pcs.empty()) {
          return false;
        }
        const bool last       {p + 1 == pos};
        bool       at_pos     {false};
        const auto byte_value {static_cast<std::uint8_t>(text_[p])};
        for (const std::int32_t pc : clist->pcs) {
          const instr& in      {prog_.code[static_cast<std::size_t>(pc)]};
          // Parked pcs are only consuming ops; the ternary's else assumes klass (sub_add_thread
          // parks nothing else). The assert makes that invariant explicit (no-op under NDEBUG).
          assert((in.op == opcode::byte || in.op == opcode::klass) && "lookaround parked a non-consuming op");
          const bool   consume {in.op == opcode::byte ? byte_value == in.arg8
                                                      : prog_.classes[in.arg16].test(byte_value)};
          if (consume) {
            sub_add_thread(*nlist, pc + 1, p + 1, last ? at_pos : sink);
          }
        }
        if (last) {
          return at_pos; // a match counts only when it ends exactly at pos
        }
        thread_list* const done {clist};
        clist = nlist;
        nlist = done;
        nlist->reset(code_size);
      }
      return false; // intentionally uncovered: the p+1==pos iteration always returns above
    }

    /*!
     * \brief Epsilon-closure for the lookaround sub-VM, on the isolated sub-scratch.
     *
     * Parks consuming (`byte`/`klass`) program counters in \p list and sets \p matched on
     * reaching the sub's `match`. A capture-free sub emits no `save` (handled defensively as
     * epsilon) and no `assert_lookaround` (nesting is rejected at compile time). Touches only
     * `state_.lookaround.stack`, never the main `state_`. Linearity: `mark_seen` dedups
     * epsilon threads within a generation; once `p` advances, the same (pc,p) cannot recur,
     * so each `assert_lookaround` is evaluated at most once per position → O(n·k·L). No memo
     * table is needed (it would be redundant and break constexpr).
     *
     * \param[in,out] list    The sub thread list to populate.
     * \param[in]     pc0     The sub-program counter to seed from.
     * \param[in]     pos     The current input position (for assertions).
     * \param[in,out] matched Set to `true` if the sub's `match` is reachable here.
     */
    constexpr void sub_add_thread(thread_list& list,
                                  std::int32_t pc0,
                                  std::size_t  pos,
                                  bool&        matched)
    {
      auto& stack {state_.lookaround.stack};
      stack.clear();
      stack.push_back({.pc = pc0, .slot = 0, .restore_value = 0});
      while (!stack.empty()) {
        const std::int32_t pc {stack.back().pc};
        stack.pop_back();
        if (list.seen(pc)) {
          continue;
        }
        list.mark_seen(pc);
        const instr& in {prog_.code[static_cast<std::size_t>(pc)]};
        switch (in.op) {
          case opcode::jump:
            stack.push_back({.pc = in.primary_target, .slot = 0, .restore_value = 0});
            break;
          case opcode::split:
            stack.push_back({.pc = in.secondary_target, .slot = 0, .restore_value = 0});
            stack.push_back({.pc = in.primary_target, .slot = 0, .restore_value = 0});
            break;
          case opcode::save:
            // intentionally uncovered: a capture-free sub emits no `save`; kept as an
            // epsilon arm for completeness should the emission ever change.
            stack.push_back({.pc = pc + 1, .slot = 0, .restore_value = 0});
            break;
          case opcode::assert_position:
            if (assertion_holds(static_cast<assert_kind>(in.arg8), pos)) {
              stack.push_back({.pc = pc + 1, .slot = 0, .restore_value = 0});
            }
            break;
          case opcode::match:
            matched = true;
            break;
          case opcode::byte:
          case opcode::klass:
            list.pcs.push_back(pc);
            break;
          case opcode::assert_lookaround:
            // intentionally uncovered: -Wswitch exhaustiveness arm; nesting is rejected at
            // parse time, so a sub-program never contains an assert_lookaround.
            break;
        }
      }
    }
  };
} // namespace real::detail

#endif // REAL_PIKE_HPP
