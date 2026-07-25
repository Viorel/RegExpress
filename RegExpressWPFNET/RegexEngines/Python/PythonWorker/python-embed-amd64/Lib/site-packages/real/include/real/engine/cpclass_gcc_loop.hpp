// O2r-1b: gcc-only body for real::detail::pike_vm::run_cp_class_loop's hot greedy loop. Full rationale
// and measured numbers in cpclass_gcc.hpp (cp_class_hi_width, called below for the >= 0x80 case).
//
// Internal — do not include directly. A body fragment, not a standalone translation unit: valid only
// spliced into run_cp_class_loop's body by pike.hpp, under the same
// #if defined(__GNUC__) && !defined(__clang__) that guards this #include. cp_index, text, mode and
// out_slots are the enclosing function's parameters/locals; asc, width and extend_run are consumed by
// the shared tail (Arc B-2 wb handling) that follows this splice in pike.hpp.

const std::uint8_t* const asc {cp_ascii_table(cp_index)};
const auto                width = [&](std::size_t i) -> std::size_t {
                                    const auto lead {static_cast<std::uint8_t>(text[i])};
                                    if (lead < 0x80U) {
                                      return asc[lead] != 0U ? std::size_t {1} : std::size_t {0};
                                    }
                                    return cp_class_hi_width(text, i, cp_index);
                                  };
// Success: fill_span_slots ensure_size; fail assigns below (shared fail lambda after this splice).
const auto extend_run = [&](std::size_t match_start) -> std::size_t {
                          const std::size_t first {width(match_start)};
                          if (first == 0) {
                            return npos;
                          }
                          std::size_t match_end {match_start + first};
                          if (prog_.hints.greedy_cp_class_plus) {
                            while (match_end < text.size()) {
                              const auto lead {static_cast<std::uint8_t>(text[match_end])};
                              if (lead < 0x80U) {
                                if (asc[lead] == 0U) {
                                  break;
                                }
                                ++match_end;
                                continue;
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
