/*!
 * \file re2/arg.hpp
 * \brief `real::compat::re2` — `RE2::Arg`, the type-erased submatch-parser cell.
 *
 * Mirrors real RE2's own mechanism (`re2.h`, `class RE2::Arg`): a `void*` destination plus a
 * function-pointer parser, so `FullMatch`/`PartialMatch`/`Consume`/`FindAndConsume` can accept a
 * heterogeneous, variadic list of output pointers (`&i`, `&s`, `&d`, …) without a virtual call or
 * an `any`-style allocation. Included via the `re2/re2.hpp` umbrella — do not include directly.
 */
#ifndef REAL_RE2_ARG_HPP
#define REAL_RE2_ARG_HPP

// Internal — do not include directly.
// Users: #include <real/compat/re2/re2.hpp>.

#include <real/version.hpp>

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

//! \brief Drop-in replacement for RE2's API surface, on REAL's engine.
namespace real::compat::re2 {

  /*!
   * \brief A type-erased destination for one captured submatch, built implicitly from `T*`.
   *
   * Constructed implicitly from a pointer to any supported destination type (`std::string`,
   * `std::string_view`, `bool`, or an integral/floating-point type), or from `nullptr` to skip a
   * group without extracting it. `FullMatch`/`PartialMatch`/`Consume`/`FindAndConsume` take these
   * by value in a variadic pack; each successful `parse` writes into the pointee, a failed one
   * (a submatch that does not convert, e.g. text into an `int`) fails the whole match call.
   */
  class Arg
  {
  public:

    //! \brief A parser: writes the `[text, text + length)` submatch into `dest`, or fails.
    using Parser = bool (*)(const char* text, std::size_t length, void* dest);

    /*!
     * \brief Default-constructs a no-op `Arg` (same as `Arg(nullptr)`).
     */
    constexpr Arg() noexcept
      : Arg(nullptr)
    {}

    /*!
     * \brief From `nullptr` — skips this submatch (always "succeeds", writes nothing).
     */
    constexpr Arg(std::nullptr_t) noexcept
      : dest_(nullptr),
        parser_(&parse_nothing)
    {}

    /*!
     * \brief From a destination pointer — the common case (`&i`, `&s`, `&d`, …).
     * \tparam T The pointee type: `std::string`, `std::string_view`, `bool`, or an
     *           integral/floating-point type.
     * \param[in] dest The destination; must outlive the call this `Arg` is passed to.
     */
    template <typename T>
    Arg(T* dest) noexcept
      : dest_(dest),
        parser_(&parse_into<T>)
    {}

    /*!
     * \brief From an explicit destination and parser — the escape hatch for a custom radix or a
     *        caller-supplied type not covered by the `T*` constructor.
     * \param[in] dest   The destination (opaque to `Arg`; interpreted only by \p parser).
     * \param[in] parser The parser called on `parse`.
     */
    constexpr Arg(void*  dest,
                  Parser parser) noexcept
      : dest_(dest),
        parser_(parser)
    {}

    /*!
     * \brief Parses `[text, text + length)` into the destination.
     * \param[in] text   The submatch's first byte (not necessarily NUL-terminated).
     * \param[in] length The submatch's length in bytes.
     * \return `true` on success (or a skipped/`nullptr` destination); `false` if the submatch
     *         does not convert to the destination type.
     */
    [[nodiscard]] bool parse(const char*       text,
                             std::size_t       length) const
    {
      return parser_(text, length, dest_);
    }

  private:

    /*!
     * \brief The `nullptr`-destination parser: always succeeds, writes nothing.
     * \return `true`, unconditionally.
     */
    static bool parse_nothing(const char* /*text*/,
                              std::size_t /*length*/,
                              void* /*dest*/)
    {
      return true;
    }

    /*!
     * \brief Parses into a `bool`: `"1"`/`"true"` -> `true`, `"0"`/`"false"` -> `false`, else fails.
     * \param[in]  text   The submatch's first byte.
     * \param[in]  length The submatch's length in bytes.
     * \param[out] out    Set on success only.
     * \return `true` on success.
     */
    static bool parse_bool(const char* text,
                           std::size_t length,
                           bool&       out)
    {
      const std::string_view view(text, length);
      if (view == "1" || view == "true") {
        out = true;
        return true;
      }
      if (view == "0" || view == "false") {
        out = false;
        return true;
      }
      return false;
    }

    /*!
     * \brief Parses into an integral type via `std::from_chars` (base 10, locale-free).
     * \tparam    T      An integral destination type.
     * \param[in]  text   The submatch's first byte.
     * \param[in]  length The submatch's length in bytes.
     * \param[out] out    Set on success only.
     * \return `true` on success (the whole submatch consumed, no overflow).
     */
    template <typename T>
    static bool parse_integral(const char* text,
                               std::size_t length,
                               T&          out)
    {
      if (length == 0) {
        return false;
      }
      const auto [ptr, ec] {std::from_chars(text, text + length, out)};
      return ec == std::errc {} && ptr == text + length;
    }

    /*!
     * \brief Parses into a floating-point type via `std::strtod` on a NUL-terminated buffer.
     *
     * Not `std::from_chars`: this SDK's libc++ (Apple Clang) has the floating-point overload of
     * `std::from_chars` explicitly deleted — a libc++/toolchain portability gap, not specific to
     * this project or to RE2. `strtod` is locale-sensitive in general, but this call site only
     * ever sees digits/`.`/`e`/`+`/`-` from a regex submatch, so the "C" locale's behavior (the
     * only one that matters here) is what `strtod` gives regardless of the process locale.
     * \tparam    T      A floating-point destination type.
     * \param[in]  text   The submatch's first byte.
     * \param[in]  length The submatch's length in bytes.
     * \param[out] out    Set on success only.
     * \return `true` on success (the whole submatch consumed, in range).
     */
    template <typename T>
    static bool parse_floating(const char* text,
                               std::size_t length,
                               T&          out)
    {
      if (length == 0) {
        return false;
      }
      const std::string buffer(text, length);
      errno = 0;
      char*        end   {};
      const double value {std::strtod(buffer.c_str(), &end)};
      if (end != buffer.c_str() + buffer.size() || errno == ERANGE) {
        return false;
      }
      out = static_cast<T>(value);
      return true;
    }

    /*!
     * \brief Dispatches to the parser for `T` — the function stored as this `Arg`'s `Parser`.
     * \tparam T   The destination type.
     * \param[in]  text   The submatch's first byte.
     * \param[in]  length The submatch's length in bytes.
     * \param[out] dest   The destination, reinterpreted as `T*`.
     * \return `true` on success.
     */
    template <typename T>
    static bool parse_into(const char* text,
                           std::size_t length,
                           void*       dest)
    {
      if constexpr (std::is_same_v<T, std::string>) {
        static_cast<std::string*>(dest)->assign(text, length);
        return true;
      }
      else if constexpr (std::is_same_v<T, std::string_view>) {
        *static_cast<std::string_view*>(dest) = std::string_view(text, length);
        return true;
      }
      else if constexpr (std::is_same_v<T, bool>) {
        return parse_bool(text, length, *static_cast<bool*>(dest));
      }
      else if constexpr (std::is_floating_point_v<T>) {
        return parse_floating<T>(text, length, *static_cast<T*>(dest));
      }
      else if constexpr (std::is_integral_v<T>) {
        return parse_integral<T>(text, length, *static_cast<T*>(dest));
      }
      else {
        static_assert(sizeof(T) == 0, "real::compat::re2::Arg: unsupported destination type");
        return false;
      }
    }

    void*  dest_;   //!< The type-erased destination (`nullptr` = skip this submatch).
    Parser parser_; //!< The parser to call on `parse` (never `nullptr`).
  };
} // namespace real::compat::re2

#endif // REAL_RE2_ARG_HPP
