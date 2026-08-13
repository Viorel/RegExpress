/*!
 * \file profile.hpp
 * \brief Optional route-attribution counters for the P0 profiling substrate.
 *
 * **Codegen-invisible when OFF** (default): tick helpers are empty \c always_inline
 * functions — no branch, no thread_local touch, no object-layout change. Counters and
 * harness helpers (\c tls / \c reset / \c snapshot / name tables) exist only under
 * \c -DREAL_PROFILE so coverage builds never see dead instrumented paths.
 *
 * Protocol: ns/B is always measured on a profile-OFF build; route counters come
 * from a profile-ON build of the same (pattern, corpus, surface). Never mix.
 *
 * Constexpr-safe when ON: increments run only outside constant evaluation
 * (guarded so static_regex stays pure).
 */
#ifndef REAL_CORE_PROFILE_HPP
#define REAL_CORE_PROFILE_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>

#include <cstdint>
#include <type_traits>

//! \brief Opt-in route/work counters, compiled out unless the profiling build flag is set.
namespace real::detail::prof {

  /*!
   * \brief Named once-per-dispatch routes (Tier 1). Always visible so tick call sites compile OFF.
   */
  enum class route : std::uint8_t
  {
    class_loop = 0,
    cp_class_loop,
    exact_literal,
    inner_literal,
    fixed_shape,
    fixed_shape_pair,  //!< heterogeneous fixed shape, two-position vector prefilter.
    codepoint_class,
    alternation,
    aho_corasick,      //!< multi-literal automaton, past the measured branch-count threshold.
    onepass_full,
    onepass_window,
    lazy_dfa_anchored, //!< A2: first-byte candidate + anchored_end
    lazy_dfa_fwd_rev,  //!< unanchored forward + reverse
    general_full,
    general_window,
    trailing_la,
    possessive_byte_loop,     //!< R2 (phase Raffinement): bare/suffixed possessive literal-byte +/++ (byte_loop_possessive).
    possessive_class_loop,    //!< bare/suffixed possessive class+/++ (klass_loop_possessive).
    possessive_cp_class_loop, //!< bare/suffixed possessive cp-class +/++ (klass_cp_loop_possessive).
    possessive_delimited,     //!< literal-prefix + possessive loop + literal-suffix ("quoted").
    count_
  };

  /*!
   * \brief Secondary events (prefilter / Arc B / abandon).
   */
  enum class event : std::uint8_t
  {
    first_byte_skip = 0,
    cascade,
    rare_byte,
    memmem,
    wb_b1_drop,     //!< compile-time drop observed at first dispatch
    wb_b2_wrap,     //!< runtime wrap: class/cp loop with wb_lead|wb_trail
    il_abandoned,
    pool_incref,    //!< COW capture-block refcount taken (one per `split` in the epsilon walk)
    pool_decref,    //!< COW capture-block refcount dropped (one per thread death)
    pool_cow_write, //!< COW capture-block written (one per `save`, group 0 included)
    count_
  };

#if defined(REAL_PROFILE)

  //! \brief Thread-local counters — never stored on engine objects. Profile-ON only.
  struct counters
  {
    std::uint64_t routes[static_cast<std::size_t>(route::count_)] {};
    std::uint64_t events[static_cast<std::size_t>(event::count_)] {};
    std::uint64_t bytes_examined                                  {};
    std::uint64_t prefilter_candidates                            {};
    std::uint64_t prefilter_rejected                              {};
    std::uint64_t run_len_hist[8]                                 {}; //!< log2 buckets for maximal class/cp runs
    //! \brief log2 buckets for the LIVE THREAD COUNT the general VM carries into each `step()`.
    //!
    //! The question this exists for: a general-VM step measures 12 to 25 ns per byte where a routed class
    //! loop measures ~1, and whether that is the list machinery or genuine NFA parallelism depends entirely
    //! on how many threads are actually live. One thread per position means the cost is overhead the shape
    //! does not need; many means it is the automaton doing real work.
    std::uint64_t thread_hist[8]                                  {};
  };

  [[nodiscard]] inline counters& tls() noexcept
  {
    static thread_local counters c;
    return c;
  }

  inline void reset() noexcept
  {
    tls() = counters {};
  }

  [[nodiscard]] inline const counters& snapshot() noexcept
  {
    return tls();
  }

  inline void record_route(route r) noexcept
  {
    ++tls().routes[static_cast<std::size_t>(r)];
  }

  inline void record_event(event e) noexcept
  {
    ++tls().events[static_cast<std::size_t>(e)];
  }

  inline void add_bytes(std::uint64_t n) noexcept
  {
    tls().bytes_examined += n;
  }

  inline void record_thread_count(std::size_t n) noexcept
  {
    unsigned    b {0};
    std::size_t x {n};
    while (x > 1 && b < 7U) {
      x >>= 1;
      ++b;
    }
    ++tls().thread_hist[b];
  }

  inline void record_prefilter_candidate() noexcept
  {
    ++tls().prefilter_candidates;
  }

  inline void record_prefilter_rejected() noexcept
  {
    ++tls().prefilter_rejected;
  }

  inline void note_run_len(std::size_t len) noexcept
  {
    unsigned    b {0};
    std::size_t x {len};
    while (x > 1 && b < 7U) {
      x >>= 1;
      ++b;
    }
    ++tls().run_len_hist[b];
  }

  [[nodiscard]] inline const char* route_name(route r) noexcept
  {
    switch (r) {
      case route::class_loop: return "class_loop";
      case route::cp_class_loop: return "cp_class_loop";
      case route::exact_literal: return "exact_literal";
      case route::inner_literal: return "inner_literal";
      case route::fixed_shape: return "fixed_shape";
      case route::fixed_shape_pair: return "fixed_shape_pair";
      case route::codepoint_class: return "codepoint_class";
      case route::alternation: return "alternation";
      case route::aho_corasick: return "aho_corasick";
      case route::onepass_full: return "onepass_full";
      case route::onepass_window: return "onepass_window";
      case route::lazy_dfa_anchored: return "lazy_dfa_anchored";
      case route::lazy_dfa_fwd_rev: return "lazy_dfa_fwd_rev";
      case route::general_full: return "general_full";
      case route::general_window: return "general_window";
      case route::trailing_la: return "trailing_la";
      case route::possessive_byte_loop: return "possessive_byte_loop";
      case route::possessive_class_loop: return "possessive_class_loop";
      case route::possessive_cp_class_loop: return "possessive_cp_class_loop";
      case route::possessive_delimited: return "possessive_delimited";
      case route::count_: return "?";
    }
    return "?";
  }

  [[nodiscard]] inline const char* event_name(event e) noexcept
  {
    switch (e) {
      case event::first_byte_skip: return "first_byte_skip";
      case event::cascade: return "cascade";
      case event::rare_byte: return "rare_byte";
      case event::memmem: return "memmem";
      case event::wb_b1_drop: return "wb_b1_drop";
      case event::wb_b2_wrap: return "wb_b2_wrap";
      case event::il_abandoned: return "il_abandoned";
      case event::pool_incref: return "pool_incref";
      case event::pool_decref: return "pool_decref";
      case event::pool_cow_write: return "pool_cow_write";
      case event::count_: return "?";
    }
    return "?";
  }

#endif // REAL_PROFILE

  // Tick helpers: always_inline so OFF builds erase the call (P3c: no dead runtime branch).
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Bill one dispatch to route \p r. Erased entirely unless \c REAL_PROFILE is defined.
   * \param[in] r The route that handled the search.
   */
  constexpr void tick_route(route r) noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      record_route(r);
    }
#else
    (void)r;
#endif
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Bill one occurrence of \p e. Erased entirely unless \c REAL_PROFILE is defined.
   * \param[in] e The event to count.
   */
  constexpr void tick_event(event e) noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      record_event(e);
    }
#else
    (void)e;
#endif
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Bill one `step()` carrying \p n live threads. Erased entirely unless \c REAL_PROFILE is defined.
   * \param[in] n Threads in the current list as the step begins.
   */
  constexpr void tick_thread_count(std::size_t n) noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      record_thread_count(n);
    }
#else
    (void)n;
#endif
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Bill one candidate a prefilter produced. Erased entirely unless \c REAL_PROFILE is defined.
   *
   * WIRED LATE, AND THE COUNTER IT FILLS WAS DEAD BEFORE. `counters::prefilter_candidates` and
   * `counters::prefilter_rejected` (visible only in a \c REAL_PROFILE build, which is why they are named
   * here as code rather than cross-referenced) were declared and incremented nowhere, so a probe that read
   * them got a false zero -- which is exactly what happened while diagnosing whether a head literal is
   * prefiltered at all: the reading was the instrument's silence, not the engine's answer.
   *
   * SCOPE, STATED because a counter whose meaning is guessed is barely better than a dead one: these two
   * cover the INNER-LITERAL route's memmem loop and nothing else. One candidate is one hit `find_literal`
   * returned; one rejection is one hit whose reverse walk reached no match start, so the loop advanced.
   * Other routes have their own notions of a candidate and are deliberately not folded in here.
   */
  constexpr void tick_prefilter_candidate() noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      record_prefilter_candidate();
    }
#endif
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Bill one prefilter candidate REJECTED by confirmation. See \ref tick_prefilter_candidate
   *        for the scope these two share.
   */
  constexpr void tick_prefilter_rejected() noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      record_prefilter_rejected();
    }
#endif
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Bill \p n scanned bytes. Erased entirely unless \c REAL_PROFILE is defined.
   * \param[in] n Bytes the caller just consumed.
   */
  constexpr void tick_bytes(std::uint64_t n) noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      add_bytes(n);
    }
#else
    (void)n;
#endif
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
  /*!
   * \brief Record a run of \p n accepted units. Erased entirely unless \c REAL_PROFILE is defined.
   * \param[in] n The run's length.
   */
  constexpr void tick_run_len(std::size_t n) noexcept
  {
#if defined(REAL_PROFILE)
    if (!std::is_constant_evaluated()) {
      note_run_len(n);
    }
#else
    (void)n;
#endif
  }
} // namespace real::detail::prof

#endif // REAL_CORE_PROFILE_HPP
