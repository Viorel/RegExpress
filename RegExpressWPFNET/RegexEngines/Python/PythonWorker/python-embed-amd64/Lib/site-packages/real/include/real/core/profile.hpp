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

namespace real::detail::prof {

  //! \brief Named once-per-dispatch routes (Tier 1). Always visible so tick call sites compile OFF.
  enum class route : std::uint8_t
  {
    class_loop = 0,
    cp_class_loop,
    exact_literal,
    inner_literal,
    fixed_shape,
    codepoint_class,
    alternation,
    onepass_full,
    onepass_window,
    lazy_dfa_anchored, //!< A2: first-byte candidate + anchored_end
    lazy_dfa_fwd_rev,  //!< unanchored forward + reverse
    general_full,
    general_window,
    trailing_la,
    count_
  };

  //! \brief Secondary events (prefilter / Arc B / abandon).
  enum class event : std::uint8_t
  {
    first_byte_skip = 0,
    cascade,
    rare_byte,
    memmem,
    wb_b1_drop, //!< compile-time drop observed at first dispatch
    wb_b2_wrap, //!< runtime wrap: class/cp loop with wb_lead|wb_trail
    il_abandoned,
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
      case route::codepoint_class: return "codepoint_class";
      case route::alternation: return "alternation";
      case route::onepass_full: return "onepass_full";
      case route::onepass_window: return "onepass_window";
      case route::lazy_dfa_anchored: return "lazy_dfa_anchored";
      case route::lazy_dfa_fwd_rev: return "lazy_dfa_fwd_rev";
      case route::general_full: return "general_full";
      case route::general_window: return "general_window";
      case route::trailing_la: return "trailing_la";
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
      case event::count_: return "?";
    }
    return "?";
  }

#endif // REAL_PROFILE

  // Tick helpers: always_inline so OFF builds erase the call (P3c: no dead runtime branch).
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((always_inline))
#endif
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
