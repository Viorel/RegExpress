/*!
 * \file utf8.hpp
 * \brief UTF-8 position arithmetic for match iteration.
 *
 * The matching engine never needs this — multi-byte constructs are compiled
 * to byte-level alternatives. It is used only by match iteration to advance
 * past an empty match by one whole codepoint, matching Python's behaviour.
 */
#ifndef REAL_UTF8_HPP
#define REAL_UTF8_HPP

#include "version.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace real::detail {

  /*!
   * \brief Number of bytes from \p pos to the start of the next codepoint.
   *
   * Reads the lead byte at \p pos to determine the sequence length, then
   * consumes any UTF-8 continuation bytes (`10xxxxxx`) up to that length.
   * Invalid or truncated sequences advance by a single byte, so the result is
   * always in `[1, 4]` and forward progress is guaranteed.
   *
   * \param[in] text The subject text.
   * \param[in] pos  Index of the codepoint's lead byte; must be < text.size().
   * \return The codepoint's byte length, in `[1, 4]`.
   */
  constexpr std::size_t codepoint_advance(std::string_view text,
                                          std::size_t      pos)
  {
    const auto  lead   {static_cast<std::uint8_t>(text[pos])};
    std::size_t length {1};
    if (lead >= 0xF0) {
      length = 4;
    }
    else if (lead >= 0xE0) {
      length = 3;
    }
    else if (lead >= 0xC0) {
      length = 2;
    }
    std::size_t       i     {pos + 1};
    const std::size_t limit {pos + length < text.size() ? pos + length : text.size()};
    while (i < limit && (static_cast<unsigned>(static_cast<std::uint8_t>(text[i])) & 0xC0U) ==
           0x80U) {
      ++i; // skip UTF-8 continuation bytes (10xxxxxx)
    }
    return i - pos;
  }
} // namespace real::detail

#endif // REAL_UTF8_HPP
