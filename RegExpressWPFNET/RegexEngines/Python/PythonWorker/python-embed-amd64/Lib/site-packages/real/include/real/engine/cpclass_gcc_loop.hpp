// gcc-only body for real::detail::pike_vm::run_cp_class_loop's hot greedy loop. Full rationale in
// cpclass_gcc.hpp, which also defines cp_class_hi_width, called below for the >= 0x80 case.
//
// Internal — do not include directly. A body fragment, not a standalone translation unit: valid only
// spliced into run_cp_class_loop's body by pike.hpp, under the same
// #if defined(__GNUC__) && !defined(__clang__) that guards this #include. cp_index, text, mode and
// out_slots are the enclosing function's parameters/locals; asc, width, in_class and extend_run are
// consumed by the word-boundary tail that follows this splice in pike.hpp.

const std::uint8_t* const asc {cp_ascii_table(cp_index)};
const auto                width = [&](std::size_t i) -> std::size_t {
                                    const auto lead {static_cast<std::uint8_t>(text[i])};
                                    // Table FIRST: `asc` is a full 256-entry row and a code-point class
                                    // never sets a bit at or above 0x80, so a hit is necessarily a
                                    // single-byte member and the `< 0x80` test cannot change the answer.
                                    // Moving it after the table takes it off the accepted-byte path.
                                    if (asc[lead] != 0U) {
                                      return 1;
                                    }
                                    if (lead < 0x80U) {
                                      return 0; // an ASCII byte this class does not hold
                                    }
                                    return cp_class_hi_width(text, i, cp_index);
                                  };
// The shared tail's scan predicate. It DELEGATES here, where the clang/MSVC body specialises: this
// body's `width` already takes the ASCII branch first, so a specialised copy has nothing left to narrow
// and only adds a second lambda for the compiler to place. Measured as instruction-count neutral across
// the class-loop rows, so the simpler form wins.
const auto in_class = [&](std::size_t i) -> bool { return width(i) != 0; };
// Success: fill_span_slots ensure_size; fail assigns below (shared fail lambda after this splice).
// Explicit by-VALUE capture of the scalars, not `[&]`: gcc keeps this lambda out of line, and a
// by-reference closure makes every call traverse references -- including one to `width`, itself a
// by-reference closure, so the width call inside is a second indirection. Small by-value members let
// the one remaining call load them directly.
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
                              // Table FIRST — same soundness argument as `width` above.
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
                          // A COUNTED repeat sits in the `else`: the greedy form is the common one, and
                          // testing it first keeps the counted branch off the hot path. pike.hpp's own
                          // extend_run carries the same split for the same reason.
                          else if (max_len != 0) {
                            for (std::size_t n {1}; n < max_len && match_end < text.size(); ++n) {
                              const auto lead {static_cast<std::uint8_t>(text[match_end])};
                              // Table FIRST — same soundness argument as `width` above.
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
