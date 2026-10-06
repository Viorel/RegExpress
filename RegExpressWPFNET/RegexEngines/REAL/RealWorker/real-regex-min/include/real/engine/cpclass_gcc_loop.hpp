// gcc-only body for real::detail::pike_vm::run_cp_class_loop's hot greedy loop; rationale in
// cpclass_gcc.hpp, which defines cp_class_hi_width.
//
// Internal — do not include directly. A body fragment spliced into run_cp_class_loop's body by pike.hpp
// under the same gcc-only #if. text and cp_index are the enclosing function's; width, in_class and
// extend_run are consumed by the tail that follows the splice.

#if !defined(REAL_CPCLASS_FRAGMENT_SITE)
#  error "real/engine/cpclass_gcc_loop.hpp is a fragment of real/engine/pike.hpp, not a header: include <real/real.hpp>"
#endif

const std::uint8_t* const asc {cp_ascii_table(cp_index)};
const auto                width = [&](std::size_t i) -> std::size_t {
                                    const auto lead {static_cast<std::uint8_t>(text[i])};
                                    // Table first: `asc` never sets a bit at or above 0x80, so a hit is
                                    // a single-byte member and `< 0x80` stays off the accepted-byte path.
                                    if (asc[lead] != 0U) {
                                      return 1;
                                    }
                                    if (lead < 0x80U) {
                                      return 0; // an ASCII byte this class does not hold
                                    }
                                    return cp_class_hi_width(text, i, cp_index);
                                  };
// Delegates to `width`, unlike the clang/MSVC body: `width` already tests the table first, and a
// specialised copy measured instruction-count neutral.
const auto in_class = [&](std::size_t i) -> bool { return width(i) != 0; };
// By-value captures, not `[&]`: gcc keeps this lambda out of line, where a reference closure costs an
// indirection per call, and a second through `width` (hence `width_at`).
const auto extend_run = [text, asc, cp_index, this,
                         greedy = prog_.hints.greedy_cp_class_plus,
                         max_len = prog_.hints.greedy_cp_class_max](std::size_t match_start) -> std::size_t {
                          const auto width_at = [text, asc, cp_index, this](std::size_t i) -> std::size_t {
                                                  const auto lead {static_cast<std::uint8_t>(text[i])};
                                                  if (asc[lead] != 0U) { return 1; }
                                                  if (lead < 0x80U) { return 0; }
                                                  return cp_class_hi_width(text, i, cp_index);
                                                };
                          const std::size_t first {width_at(match_start)};
                          if (first == 0) {
                            return npos;
                          }
                          std::size_t match_end {match_start + first};
                          if (greedy) {
                            while (match_end < text.size()) {
                              const auto lead {static_cast<std::uint8_t>(text[match_end])};
                              // Table first: see `width`.
                              if (asc[lead] != 0U) {
                                ++match_end;
                                continue;
                              }
                              if (lead < 0x80U) {
                                break; // an ASCII byte this class does not hold: the run ends
                              }
                              const std::size_t w {cp_class_hi_width(text, match_end, cp_index)};
                              if (w == 0) {
                                break;
                              }
                              match_end += w;
                            }
                          }
                          // Counted repeat in the `else`: the common greedy form stays first.
                          else if (max_len != 0) {
                            for (std::size_t n {1}; n < max_len && match_end < text.size(); ++n) {
                              const auto lead {static_cast<std::uint8_t>(text[match_end])};
                              // Table first: see `width`.
                              if (asc[lead] != 0U) {
                                ++match_end;
                                continue;
                              }
                              if (lead < 0x80U) {
                                break; // an ASCII byte this class does not hold: the run ends
                              }
                              const std::size_t w {cp_class_hi_width(text, match_end, cp_index)};
                              if (w == 0) {
                                break;
                              }
                              match_end += w;
                            }
                          }
                          return match_end;
                        };
