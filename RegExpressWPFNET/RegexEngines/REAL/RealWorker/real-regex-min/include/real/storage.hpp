/*!
 * \file storage.hpp
 * \brief Storage policies: where a program lives and how scratch is allocated.
 *
 * - \ref real::detail::dynamic_storage — everything sized at run time,
 *   exactly once, on the heap (backs `real::regex`).
 * - \ref real::detail::static_storage — the pattern is compiled at compile
 *   time into static constexpr arrays of exact size, and match scratch lives
 *   on the stack: zero allocations (backs `real::static_regex`).
 *
 * Exact sizing uses C++20 transient constexpr allocation: the program is
 * built once to measure each array, then rebuilt to fill it.
 */
#ifndef REAL_STORAGE_HPP
#define REAL_STORAGE_HPP

// Internal — do not include directly.
// Users: #include <real/real.hpp>, or a documented opt-in: <real/dfa.hpp>,
// <real/regex_set.hpp>, <real/compat/std/regex.hpp>, <real/compat/re2/re2.hpp>.

#include "real/version.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "real/frontend/ast.hpp"
#include "real/frontend/compiler.hpp"
#include "real/engine/pike.hpp"
#include "real/core/program.hpp"

namespace real {

  /*!
   * \brief A fixed-size string usable as a non-type template parameter.
   *
   * Enables `static_regex<"\d+">`: the literal is captured into \ref data at
   * compile time.
   *
   * \tparam N Size of the character array, including the terminating NUL.
   */
  template <std::size_t N>
  struct fixed_string
  {
    char data[N] = {}; //!< The captured characters, including the trailing NUL.

    /*!
     * \brief Captures a string literal.
     *
     * Implicit by design: it is what lets a string literal be a non-type
     * template argument; marking it `explicit` would defeat the purpose.
     *
     * \param[in] literal The string literal to capture.
     */
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    constexpr fixed_string(const char (&literal)[N])
    {
      for (std::size_t i = 0; i < N; ++i) {
        data[i] = literal[i];
      }
    }

    /*!
     * \brief Returns a view of the string, excluding the trailing NUL.
     * \return A view of the \c N-1 pattern characters.
     */
    [[nodiscard]] constexpr std::string_view view() const
    {
      return {data, N - 1};
    }
  };

  /*! \brief Storage-policy internals shared by \ref real::regex and \ref real::static_regex. */
  namespace detail {

    /*!
     * \brief Fixed-capacity vector backed by an inline array (no heap).
     *
     * The subset of `std::vector` the Pike VM uses, for the static storage mode.
     * Overflow cannot happen for the engine's own containers: `static_storage` sizes each
     * one exactly via its measure pass, so the `length_error` guards are an unreachable
     * structural safety net — kept deliberately, and never hit at run time (hence not
     * covered by the runtime coverage report).
     *
     * \tparam T   Element type.
     * \tparam Cap Inline capacity.
     */
    template <typename T, std::size_t Cap>
    class static_vec
    {
    public:

      /*!
       * \brief Value-initializes \ref data_ during constant evaluation only.
       *
       * MSVC's constant evaluator refuses an object with an indeterminate subobject even when nothing
       * reads it: `static_regex`'s compile-time `static_assert`s failed with C2131 (`expression did not
       * evaluate to a constant`) when this storage was left trivially initialized everywhere. clang and
       * gcc accept it (C++20 P1331R2), so the runtime path — where the whole point is not paying for the
       * clear — keeps the trivial initialization on every toolchain.
       */
      constexpr static_vec() noexcept
      {
        if (std::is_constant_evaluated()) {
          data_ = {};
        }
      }

      /*!
       * \brief Appends \p value.
       * \param[in] value The element to append.
       * \throws std::length_error if the capacity `Cap` is exceeded.
       */
      constexpr void push_back(const T& value)
      {
        if (size_ == Cap) {
          throw std::length_error("static_vec overflow");
        }
        data_[size_] = value;
        ++size_;
      }

      /*!
       * \brief Removes all elements (capacity unchanged).
       */
      constexpr void clear()
      {
        size_ = 0;
      }

      /*!
       * \brief Resizes to \p count copies of \p value.
       * \param[in] count Number of elements.
       * \param[in] value The value to fill with.
       * \throws std::length_error if \p count exceeds the capacity `Cap`.
       */
      constexpr void assign(std::size_t count,
                            const T&    value)
      {
        if (count > Cap) {
          throw std::length_error("static_vec overflow");
        }
        for (std::size_t i = 0; i < count; ++i) {
          data_[i] = value;
        }
        size_ = count;
      }

      /*!
       * \brief Ensures at least \p count live elements without re-filling or shrinking.
       *
       * find_iter reuses capture storage; after the first match \c size already equals
       * the program slot count and the fast path overwrites every used index — a full
       * \ref assign of \c npos is dead work. Never shrinks (a multi-group path may have
       * already sized to \c slot_count > 2 before a span write).
       *
       * \param[in] count Minimum size.
       * \throws std::length_error if \p count exceeds the capacity `Cap`.
       */
      constexpr void ensure_size(std::size_t count)
      {
        if (size_ >= count) {
          return;
        }
        if (count > Cap) {
          throw std::length_error("static_vec overflow");
        }
        size_ = count;
      }

      /*!
       * \brief Returns the number of elements.
       * \return The element count.
       */
      [[nodiscard]] constexpr std::size_t size() const
      {
        return size_;
      }

      /*!
       * \brief Returns `true` if empty.
       * \return Whether the vector holds no elements.
       */
      [[nodiscard]] constexpr bool empty() const
      {
        return size_ == 0;
      }

      /*!
       * \brief Returns reference to the element at \p i.
       * \param[in] i Index.
       * \return Reference to the element at \p i.
       */
      [[nodiscard]] constexpr T& operator[](std::size_t i)
      {
        return data_[i];
      }

      /*!
       * \brief Returns const reference to the element at \p i.
       * \param[in] i Index.
       * \return Const reference to the element at \p i.
       */
      [[nodiscard]] constexpr const T& operator[](std::size_t i) const
      {
        return data_[i];
      }

      /*!
       * \brief Returns reference to the last element. Precondition: the vector is non-empty.
       * \return A reference to the last element.
       */
      [[nodiscard]] constexpr T& back()
      {
        assert(size_ > 0 && "back() on an empty static_vec"); // debug precondition; no-op under NDEBUG
        return data_[size_ - 1];
      }

      /*!
       * \brief Removes the last element. Precondition: the vector is non-empty.
       */
      constexpr void pop_back()
      {
        assert(size_ > 0 && "pop_back() on an empty static_vec");
        --size_;
      }

    private:

      /*!
       * \brief Inline element storage, deliberately NOT value-initialized at run time.
       *
       * `static_regex` is stateless (`sizeof` 1), so a `search()` builds a fresh state on the stack every
       * call, and value-initializing here zeroed the whole worst-case capacity each time -- a cost the
       * dynamic storage does not pay, since its state lives in the regex object and is reused. Nothing
       * reads above \ref size_, which starts at 0, so the zeros were never observed. C++20 permits the
       * trivial default initialization even in a constant expression (P1331R2); reading an element before
       * writing it would be diagnosed there rather than silently returning garbage.
       *
       * On a short subject this is most of the cost of a search: the clear is proportional to the
       * worst-case capacity while the work is proportional to the subject.
       */
      std::array<T, Cap> data_;
      std::size_t        size_ {}; //!< Number of elements in use.
    };

    /*!
     * \brief Small-buffer-optimized vector for the dynamic hot paths.
     *
     * Keeps up to `InlineCapacity` elements inline (no heap), spilling to the
     * heap beyond that — so the common small-group match avoids allocation
     * entirely. Used for capture slots and working state in the dynamic mode.
     *
     * \note \p T must be **trivially destructible** (enforced by a `static_assert`):
     *       small_vec runs no element destructors — inline elements in particular are
     *       never individually destroyed — which suits its POD-like VM use and keeps the
     *       hot path allocation- and bookkeeping-free.
     *
     * \warning **Run-time invariant — the inline buffer is left UNINITIALIZED** (value-initialized
     *          only under `std::is_constant_evaluated()`, in the `Storage` union constructor). Every
     *          element's lifetime is begun by `std::construct_at` (placement-new) before it is read,
     *          and reads stay within `[0, size_)`. Any accessor added here must preserve that
     *          write-before-read order, or it reads indeterminate memory — a silent UB
     *          value-init would mask. MemorySanitizer is the detector — the CI sanitize leg is
     *          ASan/UBSan, which does not catch this — so run an MSan build when changing how small_vec
     *          accesses its elements.
     *
     * \tparam T              Element type.
     * \tparam InlineCapacity Number of elements held inline before spilling.
     */
    template <typename T, std::size_t InlineCapacity>
    class small_vec
    {
      static_assert(InlineCapacity > 0, "InlineCapacity must be positive");
      // small_vec runs no element destructors — inline elements in particular are never
      // destroyed (cleanup() only frees the heap block). That is correct only for
      // trivially-destructible types, which is its sole use (POD-like VM state).
      static_assert(std::is_trivially_destructible_v<T>,
                    "small_vec is for trivially-destructible types only");

      // size_ and capacity_ are ALWAYS std::size_t — never a type narrowed on
      // InlineCapacity. small_vec spills to the heap and routinely holds far more than
      // its inline capacity (up to code_size in the VM: ~3·code_size epsilon entries, the
      // live thread count of a wide alternation, the 2·(groups+1) capture slots). A
      // uint8_t / uint16_t counter would truncate: at 256 / 65536, static_cast wraps the
      // capacity to 0, reserve() then sees new_cap ≤ capacity_ and no-ops, so the buffer
      // is rewritten in place and back() indexes out of bounds. The inline buffer
      // dominates sizeof, so the wide counter is free.
      std::size_t size_     {};               //!< Number of elements in use.
      std::size_t capacity_ {InlineCapacity}; //!< Current capacity.
      bool        is_heap_  {};               //!< True once spilled to the heap.

      /*!
       * \brief Inline element block. A struct (not a bare C array) so the union ctor can activate it as a
       *        whole with \c construct_at in a constant expression, while \ref inline_data still indexes a
       *        plain C array — which the static analyzer can bound (a \c std::array's `operator[]` hides
       *        the extent and trips a false out-of-bounds on \ref transfer_range).
       */
      struct inline_block
      {
        T elems[InlineCapacity]; //!< The inline elements, as a plain C array the analyzer can bound.
      };

      /*!
       * \brief The either-or storage: the inline buffer, or a pointer to the heap block once the vector
       *        has spilled. \ref is_heap_ says which member is active.
       */
      union Storage
      {
        inline_block inline_buffer; //!< Inline storage (when not heap).
        T*           heap_ptr;      //!< Heap storage (when \ref is_heap_).

        /*!
         * \brief Starts in the inline state.
         *
         * At **run time** the inline buffer is left UNINITIALIZED: small_vec writes every
         * element through \c std::construct_at (placement-new) before any read (push_back and
         * assign), so the value-init is pure overhead — and a tokenizing walk builds a fresh slot
         * buffer per match, of which two slots serve, so it is overhead per match.
         * At **compile time** the member must be active and initialized for the constexpr
         * matching path (which assigns through \ref inline_data while it is the active member), so
         * it is value-initialized there via \c construct_at on the whole \ref inline_block —
         * activating a class-type member is what a constexpr union allows (an element-wise or bare
         * C-array activation is not). A constant-evaluated spill switches the active member to
         * \ref Storage::heap_ptr through \ref adopt_heap, and \ref revert_to_inline switches back.
         */
        constexpr Storage() noexcept
        {
          if (std::is_constant_evaluated()) {
            std::construct_at(&inline_buffer);
          }
        }

        constexpr ~Storage() {} //!< Destruction handled by \ref cleanup.

        // Rule of Five, made explicit. The union holds a possibly non-trivial T, so
        // copy/move would be implicitly deleted anyway; small_vec manages copy, move
        // and element lifetimes itself (is_heap_-aware) and never copies Storage by
        // value. Declaring these deleted is also a safety guard: a defaulted copy
        // would byte-copy the union and inherit the wrong active member (double-free).
        Storage(const Storage&)             = delete;
        Storage& operator=(const Storage&)  = delete;
        Storage(Storage &&)                 = delete;
        Storage& operator=(Storage&&)       = delete;
      } storage_ {}; //!< The active storage, inline or heap.

      // Run-time cache of the active storage base (inline buffer or heap block), refreshed on
      // every state change via \ref refresh_data. The hot accessors (operator[], back, push_back)
      // index through it, so they avoid the per-access `is_heap_` branch that the profile showed
      // dominating add_thread. NOT used during constant evaluation: a pointer to a subobject of
      // *this is not a usable constant across copies, so the constexpr path keeps the is_heap_
      // branch (guarded by std::is_constant_evaluated). A dynamic `real::regex` IS constant-
      // evaluable and does use small_vec, heap spill included, so that branch is live rather than
      // theoretical -- it is the compile-time path's only way to reach the elements.
      T* data_ {}; //!< Cached base of the active storage; see the note above, and \ref refresh_data.

      /*!
       * \brief Refreshes \ref data_ to the active storage base (run time only).
       */
      constexpr void refresh_data() noexcept
      {
        if (!std::is_constant_evaluated()) {
          data_ = is_heap_ ? storage_.heap_ptr : inline_data();
        }
      }

      /*!
       * \brief Returns pointer to the inline buffer.
       * \return A pointer to its first element, whether or not the inline state is active.
       */
      [[nodiscard]] constexpr T* inline_data() noexcept
      {
        return &storage_.inline_buffer.elems[0];
      }

      /*!
       * \brief Returns const pointer to the inline buffer.
       * \return A const pointer to its first element, whether or not the inline state is active.
       */
      [[nodiscard]] constexpr const T* inline_data() const noexcept
      {
        return &storage_.inline_buffer.elems[0];
      }

      /*!
       * \brief Copies or moves \p count elements from \p src to \p dest.
       * \tparam Move If true, move-construct; otherwise copy-construct.
       * \param[in]  src   Source range.
       * \param[in]  count Element count.
       * \param[out] dest  Destination (uninitialized) range.
       */
      template <bool Move>
      constexpr void transfer_range(const T   * src,
                                    std::size_t count,
                                    T         * dest)
      {
        if constexpr (std::is_trivially_copyable_v<T>) {
          if (!std::is_constant_evaluated()) {
            std::memcpy(dest, src, count * sizeof(T));
            return;
          }
        }
        // Element-wise transfer: the constexpr path, and the run-time path for a
        // non-trivially-copyable T (the VM's POD element types take the memcpy above).
        for (std::size_t i = 0; i < count; ++i) {
          if constexpr (Move) {
            std::construct_at(&dest[i], std::move(src[i]));
          }
          else {
            std::construct_at(&dest[i], src[i]);
          }
        }
      }

      /*!
       * \brief Transfers \p other's inline elements into this vector's inline buffer.
       *
       * The copy/move paths use this when \p other has not spilled. The count is
       * clamped to `InlineCapacity` — a no-op on the value, since this runs only when
       * the elements are inline (`size_ <= InlineCapacity`) — but it lets the optimizer
       * see the inline buffer cannot overflow. Without it, g++ -O3 value-propagates a
       * spilled source's large `size_` into this then-dead branch and reports a spurious
       * `-Wstringop-overflow` on the memcpy in \ref transfer_range.
       *
       * \tparam Move If true, move-construct the elements; otherwise copy-construct.
       * \param[in] other The not-yet-spilled source vector.
       */
      template <bool Move>
      constexpr void transfer_inline_from(const small_vec& other)
      {
        const std::size_t inline_count {other.size_ <= InlineCapacity ? other.size_ : InlineCapacity};
        transfer_range<Move>(other.inline_data(), inline_count, inline_data());
      }

      /*!
       * \brief Obtains storage for \p n elements, from whichever allocator the current regime allows.
       *
       * A raw allocation operator cannot be called in a constant expression, so constant evaluation
       * goes through `std::allocator`, which C++20 makes usable there. Its blocks are *transient*:
       * every one must be released before the evaluation ends, which \ref cleanup does.
       *
       * \param[in] n Element count.
       * \return Uninitialized storage for \p n elements.
       */
      [[nodiscard]] static constexpr T* allocate_block(std::size_t n)
      {
        if (std::is_constant_evaluated()) {
          return std::allocator<T> {}.allocate(n);
        }
        return static_cast<T*>(::operator new(n * sizeof(T)));
      }

      /*!
       * \brief Releases a block from \ref allocate_block.
       * \param[in] p Block base.
       * \param[in] n The capacity it was allocated with — `std::allocator` requires the exact count.
       */
      static constexpr void deallocate_block(T         * p,
                                             std::size_t n) noexcept
      {
        if (std::is_constant_evaluated()) {
          std::allocator<T> {}.deallocate(p, n);
          return;
        }
        (void) n;
        ::operator delete(p);
      }

      /*!
       * \brief Points the union at \p p, making \ref Storage::heap_ptr the active member.
       *
       * Constant evaluation has no inactive-member assignment: switching to the pointer requires
       * beginning its lifetime, which ends the inline buffer's. The run-time statement is left as a
       * plain assignment — it is on the spill path of every container in the VM.
       *
       * \param[in] p The heap block to adopt.
       */
      constexpr void adopt_heap(T* p) noexcept
      {
        if (std::is_constant_evaluated()) {
          std::construct_at(&storage_.heap_ptr, p);
          return;
        }
        storage_.heap_ptr = p;
      }

      /*!
       * \brief Makes the inline buffer the active union member again, after the heap block is gone.
       *
       * The moved-from side of a move needs this: it is left inline, so a later constant-evaluated
       * read through \ref inline_data would otherwise touch an inactive member.
       */
      constexpr void revert_to_inline() noexcept
      {
        if (std::is_constant_evaluated()) {
          std::construct_at(&storage_.inline_buffer);
          return;
        }
        storage_.heap_ptr = nullptr;
      }

      /*!
       * \brief Frees the heap block, if any. \p T is trivially destructible (see the class
       *        `static_assert`), so no element destructors run — and inline storage needs no
       *        cleanup at all.
       */
      constexpr void cleanup() noexcept
      {
        if (is_heap_) {
          deallocate_block(storage_.heap_ptr, capacity_);
        }
      }

      /*!
       * \brief Doubles the capacity (saturating), spilling to the heap as needed.
       */
      constexpr void extend_capacity()
      {
        const std::size_t current {capacity_};
        const std::size_t new_cap {(current > std::numeric_limits<std::size_t>::max() / 2)
                                     ? std::numeric_limits<std::size_t>::max()
                                     : current * 2};
        reserve(new_cap);
      }

    public:

      using value_type = T;           //!< Element type.
      using size_type_ = std::size_t; //!< Size type (for std-container API compat).

      /*!
       * \brief Constructs an empty vector in the inline state.
       */
      constexpr small_vec() noexcept
      {
        refresh_data(); // points data_ at the inline buffer (run time)
      }

      /*!
       * \brief Destroys elements and frees any heap block.
       */
      constexpr ~small_vec()
      {
        // Unconditional: a constant-evaluated spill allocates a transient block, and leaving it
        // unreleased makes the whole constant expression ill-formed.
        cleanup();
      }

      /*!
       * \brief Appends \p value, growing to the heap if the inline buffer is full.
       * \param[in] value The element to append.
       */
      constexpr void push_back(const T& value)
      {
        if (size_ >= capacity_) {
          extend_capacity();
        }
        if (std::is_constant_evaluated()) {
          if (is_heap_) {
            std::construct_at(&storage_.heap_ptr[size_], value);
          }
          else {
            inline_data()[size_] = value;
          }
        }
        else {
          // size_ < capacity_ holds here (checked above); the analyzer cannot relate the active
          // block's allocation size to size_. data_ is the active base (branchless).
          // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
          std::construct_at(&data_[size_], value);
        }
        ++size_;
      }

      /*!
       * \brief Resizes to \p count copies of \p value.
       * \param[in] count Number of elements.
       * \param[in] value The value to fill with.
       */
      constexpr void assign(std::size_t count,
                            const T&    value)
      {
        clear();
        if (count > capacity_) {
          reserve(count);
        }
        for (std::size_t i = 0; i < count; ++i) {
          if (std::is_constant_evaluated()) {
            // Compile time: the inline buffer is value-initialized, so assign through it;
            // the heap path is never taken in a constant expression.
            if (is_heap_) {
              std::construct_at(&storage_.heap_ptr[i], value);
            }
            else {
              inline_data()[i] = value;
            }
          }
          else {
            // Run time: the inline buffer is uninitialized (see \ref Storage), so this is the
            // first write — placement-new begins each element's lifetime. data_ is the active
            // base (inline or heap), branchless like push_back.
            std::construct_at(&data_[i], value);
          }
        }
        size_ = count;
      }

      /*!
       * \brief Ensures at least \p count live elements without re-filling or shrinking.
       *
       * Fast-path find_iter: after the first match, \c size already equals the program
       * slot count and the writer overwrites every used index — a full \ref assign of \c npos
       * is dead work. Growing default-constructs only the new tail (caller must write every
       * slot it later reads). Never shrinks — multi-group fixed-shape may already be sized
       * to \c slot_count before a span-only write of slots 0/1.
       *
       * \param[in] count Minimum size.
       */
      constexpr void ensure_size(std::size_t count)
      {
        if (size_ >= count) {
          return;
        }
        if (count > capacity_) {
          reserve(count);
        }
        for (std::size_t i = size_; i < count; ++i) {
          if (std::is_constant_evaluated()) {
            if (is_heap_) {
              std::construct_at(&storage_.heap_ptr[i], T {});
            }
            else {
              inline_data()[i] = T {};
            }
          }
          else {
            std::construct_at(&data_[i], T {});
          }
        }
        size_ = count;
      }

      /*!
       * \brief Returns the number of elements.
       * \return The element count.
       */
      [[nodiscard]] constexpr std::size_t size() const noexcept
      {
        return size_;
      }

      /*!
       * \brief Returns `true` if empty.
       * \return Whether the vector holds no elements.
       */
      [[nodiscard]] constexpr bool empty() const noexcept
      {
        return size_ == 0;
      }

      /*!
       * \brief Returns reference to the element at \p i.
       * \param[in] i Index.
       * \return Reference to the element at \p i.
       */
      [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
      {
        if (std::is_constant_evaluated()) {
          return is_heap_ ? storage_.heap_ptr[i] : inline_data()[i];
        }
        return data_[i];
      }

      /*!
       * \brief Returns const reference to the element at \p i.
       * \param[in] i Index.
       * \return Const reference to the element at \p i.
       */
      [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
      {
        if (std::is_constant_evaluated()) {
          return is_heap_ ? storage_.heap_ptr[i] : inline_data()[i];
        }
        return data_[i];
      }

      /*!
       * \brief Removes all elements (capacity and heap state unchanged).
       */
      constexpr void clear() noexcept
      {
        size_ = 0;
      }

      /*!
       * \brief Returns reference to the last element. Precondition: the vector is non-empty.
       * \return A reference to the last element.
       */
      [[nodiscard]] constexpr T& back() noexcept
      {
        assert(size_ > 0 && "back() on an empty small_vec"); // debug precondition; no-op under NDEBUG
        if (std::is_constant_evaluated()) {
          return is_heap_ ? storage_.heap_ptr[size_ - 1] : inline_data()[size_ - 1];
        }
        return data_[size_ - 1];
      }

      /*!
       * \brief Returns const reference to the last element. Precondition: the vector is non-empty.
       * \return A const reference to the last element.
       */
      [[nodiscard]] constexpr const T& back() const noexcept
      {
        assert(size_ > 0 && "back() on an empty small_vec");
        if (std::is_constant_evaluated()) {
          return is_heap_ ? storage_.heap_ptr[size_ - 1] : inline_data()[size_ - 1];
        }
        return data_[size_ - 1];
      }

      /*!
       * \brief Removes the last element. Precondition: the vector is non-empty.
       *
       * For VM-internal use (POD types like size_t, eps_entry) explicit destroy is unnecessary;
       * full cleanup happens in the destructor / clear when on the heap.
       */
      constexpr void pop_back() noexcept
      {
        assert(size_ > 0 && "pop_back() on an empty small_vec");
        --size_;
      }

      /*!
       * \brief Ensures capacity for at least \p new_capacity elements (heap-backed).
       * \param[in] new_capacity Desired minimum capacity; smaller is a no-op.
       */
      constexpr void reserve(std::size_t new_capacity)
      {
        if (new_capacity <= capacity_) {
          return;
        }
        T      * new_data {allocate_block(new_capacity)};
        const T* old_data {is_heap_ ? storage_.heap_ptr : inline_data()};
        transfer_range<false>(old_data, size_, new_data);
        if (is_heap_) {
          deallocate_block(storage_.heap_ptr, capacity_);
        }
        adopt_heap(new_data); // reads of old_data are done; this ends the inline buffer's lifetime
        capacity_         = new_capacity;
        is_heap_          = true;
        refresh_data();       // run-time path only (constexpr threw above); data_ now points at the heap block
      }

      /*!
       * \brief Move constructor: steals \p other's heap block or moves inline elements.
       * \param[in,out] other The vector to move from; left empty and inline.
       */
      constexpr small_vec(small_vec&& other) noexcept
        : size_(other.size_),
          capacity_(other.capacity_),
          is_heap_(other.is_heap_)
      {
        if (is_heap_) {
          adopt_heap(other.storage_.heap_ptr);
          other.revert_to_inline(); // the block is ours now; other is left inline and empty
          other.is_heap_          = false;
          other.size_             = 0;
          other.capacity_         = InlineCapacity;
        }
        else {
          transfer_inline_from<true>(other);
        }
        refresh_data();
        other.refresh_data(); // other is now empty/inline
      }

      /*!
       * \brief Move assignment.
       * \param[in,out] other Source (left empty).
       * \return *this.
       */
      constexpr small_vec& operator=(small_vec&& other) noexcept
      {
        if (this != &other) {
          const bool was_heap {is_heap_}; // read before the assignment below overwrites it
          cleanup();
          size_     = other.size_;
          capacity_ = other.capacity_;
          is_heap_  = other.is_heap_;
          if (is_heap_) {
            adopt_heap(other.storage_.heap_ptr);
            other.revert_to_inline(); // the block is ours now; other is left inline and empty
            other.is_heap_          = false;
            other.size_             = 0;
            other.capacity_         = InlineCapacity;
          }
          else {
            if (was_heap) {
              revert_to_inline(); // cleanup() freed the block; reactivate the inline member
            }
            transfer_inline_from<true>(other);
          }
          refresh_data();
          other.refresh_data(); // other is now empty/inline
        }
        return *this;
      }

      /*!
       * \brief Copy constructor (needed for `vector<match_result>` in find_all).
       * \param[in] other The vector to copy.
       */
      constexpr small_vec(const small_vec& other)
        : size_(other.size_),
          capacity_(other.capacity_)
      {
        if (other.is_heap_) {
          adopt_heap(allocate_block(other.capacity_));
          transfer_range<false>(other.storage_.heap_ptr, other.size_, storage_.heap_ptr);
          is_heap_  = true;
          capacity_ = other.capacity_;
        }
        else {
          transfer_inline_from<false>(other);
        }
        refresh_data();
      }

      /*!
       * \brief Copy assignment.
       * \param[in] other Source.
       * \return *this.
       */
      constexpr small_vec& operator=(const small_vec& other)
      {
        if (this != &other) {
          cleanup();
          size_ = other.size_;
          if (other.is_heap_) {
            adopt_heap(allocate_block(other.capacity_));
            transfer_range<false>(other.storage_.heap_ptr, other.size_, storage_.heap_ptr);
            is_heap_  = true;
            capacity_ = other.capacity_;
          }
          else {
            if (is_heap_) {
              revert_to_inline(); // cleanup() freed the block; reactivate the inline member
            }
            is_heap_  = false;
            capacity_ = InlineCapacity;
            transfer_inline_from<false>(other);
          }
          refresh_data();
        }
        return *this;
      }
    };

    /*!
     * \brief The name-resolution context a result owns when it must outlive the regex it came from.
     *
     * A result resolves a group name by comparing it against the pattern text at the offsets its
     * named-group table records, so it borrows BOTH from the regex. The borrow is free and correct
     * while the regex is alive, which is the documented contract; on a temporary regex it is a read
     * of freed memory. A result produced from an rvalue regex copies both here instead. The copy is
     * shared, so copying such a result shares the context rather than duplicating it, and the cost
     * lands only where a pattern was just compiled -- which allocates far more than this.
     */
    struct owned_name_context
    {
      std::string                    pattern; //!< Owned copy of the pattern text.
      std::vector<named_group>       names;   //!< Owned copy of the named-group table.
    };

    /*!
     * \brief A uniquely-owning, deep-copying box for \ref owned_name_context that survives constant
     *        evaluation.
     *
     * `std::shared_ptr` is the obvious handle here and cannot be used: a `real::regex` is
     * constant-evaluable, so the result type it yields must stay literal, and no standard smart
     * pointer is. `std::allocator` is the only constexpr-usable source in C++20 -- the same reason
     * \ref small_vec grows through it -- and its blocks are transient, so this releases
     * unconditionally.
     *
     * Deep copy rather than shared: the box is null on every result that borrows, which is every
     * result a walk or an lvalue regex produces, so nothing on the path that matters ever copies
     * one. What the borrowing path pays is a pointer and a null test.
     */
    class name_context_box
    {
    public:

      /*!
       * \brief Constructs an empty box, owning nothing.
       */
      constexpr name_context_box() noexcept = default;

      /*!
       * \brief Deep-copies the other box's context, if it has one.
       * \param[in] other The box to copy.
       */
      constexpr name_context_box(const name_context_box& other)
      {
        if (other.ptr_ != nullptr) [[unlikely]] {
          adopt(*other.ptr_);
        }
      }

      /*!
       * \brief Takes over the other box's context, leaving it empty.
       * \param[in,out] other The box to move from.
       */
      constexpr name_context_box(name_context_box&& other) noexcept
        : ptr_(other.ptr_)
      {
        other.ptr_ = nullptr;
      }

      /*!
       * \brief Deep-copy assignment.
       * \param[in] other The box to copy.
       * \return `*this`.
       */
      constexpr name_context_box& operator=(const name_context_box& other)
      {
        if (this != &other) {
          reset();
          if (other.ptr_ != nullptr) [[unlikely]] {
            adopt(*other.ptr_);
          }
        }
        return *this;
      }

      /*!
       * \brief Move assignment.
       * \param[in,out] other The box to move from.
       * \return `*this`.
       */
      constexpr name_context_box& operator=(name_context_box&& other) noexcept
      {
        if (this != &other) {
          reset();
          ptr_       = other.ptr_;
          other.ptr_ = nullptr;
        }
        return *this;
      }

      /*!
       * \brief Releases the owned context, if any.
       */
      constexpr ~name_context_box()
      {
        reset();
      }

      /*!
       * \brief Replaces the owned context with one built from \p pattern and \p names.
       * \param[in] pattern The pattern text to own.
       * \param[in] names   The named-group table to own.
       */
      constexpr void emplace(std::string              pattern,
                             std::vector<named_group> names)
      {
        reset();
        ptr_ = std::allocator<owned_name_context> {}.allocate(1);
        std::construct_at(ptr_, owned_name_context {std::move(pattern), std::move(names)});
      }

      /*!
       * \brief Returns the owned context, or `nullptr` when the box is empty.
       * \return The owned context, or `nullptr`.
       */
      [[nodiscard]] constexpr const owned_name_context* get() const noexcept
      {
        return ptr_;
      }

    private:

      /*!
       * \brief Allocates and copy-constructs a context from \p src.
       *
       * Cold: only a result detached from a temporary regex ever owns a context, and only a copy of one
       * ever reaches here. Left warm it bids for the translation unit's inline budget against the scan
       * routes, and takes it from them -- the same non-monotonic budget effect \ref build_byte_program is
       * annotated for.
       *
       * \param[in] src The context to copy.
       */
      constexpr void adopt(const owned_name_context& src)
      {
        ptr_ = std::allocator<owned_name_context> {}.allocate(1);
        std::construct_at(ptr_, src);
      }

      /*!
       * \brief Releases the owned context. Precondition: the box owns one.
       *
       * Split out of \ref reset, and cold for the same reason as the copy path above: every result on the
       * borrowing path runs the null test and nothing else.
       */
      constexpr void release() noexcept
      {
        std::destroy_at(ptr_);
        std::allocator<owned_name_context> {}.deallocate(ptr_, 1);
        ptr_ = nullptr;
      }

      /*!
       * \brief Destroys and deallocates the owned context, if any.
       */
      constexpr void reset() noexcept
      {
        if (ptr_ != nullptr) [[unlikely]] {
          release();
        }
      }

      owned_name_context* ptr_ {nullptr}; //!< The owned context, or `nullptr`.
    };

    /*!
     * \brief The compile-time policy's name owner: there is nothing to own.
     *
     * `static_storage` keeps its pattern and its named-group table in `static constexpr` objects,
     * which outlive every result by construction, so a result from a temporary `static_regex`
     * borrows safely and owns nothing. The type must also stay literal: a result is constructed
     * during constant evaluation on this path, and `std::shared_ptr` cannot be.
     */
    struct borrowed_names
    {
      /*!
       * \brief Always `nullptr`: this policy owns nothing, so a result always reads its borrowed
       *        views. Constant, so the owning branch folds away entirely on this path.
       * \return `nullptr`.
       */
      [[nodiscard]] static constexpr const owned_name_context* get() noexcept
      {
        return nullptr;
      }
    };

    /*!
     * \brief Storage policy backing `real::regex`: heap, sized once at run time.
     *
     * Match scratch uses small-buffer-optimized containers, so the common
     * small-group match runs without a heap allocation.
     */
    struct dynamic_storage
    {
      static constexpr bool is_compile_time {}; //!< Selects the runtime constructor.
      /*!
       * \brief Name-resolution owner: empty unless the result outlives the regex it came from.
       */
      using name_owner = name_context_box;
      /*!
       * \brief Capture-slot container: SBO, avoiding the heap for typical small group counts.
       */
      using slot_storage = small_vec<std::size_t, 32>;
      /*!
       * \brief VM scratch state: SBO thread lists, working slots and eps stack.
       */
      struct state_type : basic_pike_state<
                            basic_thread_list<small_vec<std::int32_t, 64>,
                                              small_vec<std::size_t, 256>,
                                              // SBO like its two siblings, and it was the last heap
                                              // container in a thread list. What this buys is not
                                              // the avoided allocation -- it shows up on searches
                                              // that never allocate -- but keeping `std::vector`'s
                                              // destructor out of a state that every standalone
                                              // `search()` builds and tears down. The inline
                                              // capacity is deliberately small: growing the state
                                              // is what costs gcc/x86 its class-scan codegen, and
                                              // the saving does not depend on the capacity.
                                              //
                                              // RAISING IT TO 16 WAS TRIED AND REFUSED, on the
                                              // measurement rather than on the argument. Eight sits
                                              // below every program that reaches the general VM, so
                                              // sixteen would close only the narrow band just above
                                              // it -- and it charges every per-call row for the
                                              // wider state, which is the side that measured worse
                                              // than the target row measured better. The tier that
                                              // would actually cover the general VM's own range is
                                              // several times larger again, a worse version of the
                                              // same trade, so it was not attempted.
                                              small_vec<std::uint64_t, 8>>,
                            small_vec<eps_entry, 32>>
      {
        /*!
         * \brief Isolated sub-scratch for bounded lookaround evaluation, built on first use.
         *
         * Two thread lists and an epsilon stack, each with their own containers. A `search()` builds a
         * fresh state, so constructing and destroying all of that landed on every search — for the
         * overwhelming majority of patterns, which have no lookaround at all. Lazy: the `requires` gates
         * that gate the lookaround routes still see the member, and the sub-VM emplaces it when it first
         * actually runs.
         */
        std::optional<lookaround_scratch> lookaround;
        /*!
         * \brief Copy-on-write capture blocks, SBO rather than `pike.hpp`'s heap-vector alias.
         *
         * The pool's own `reset` comment records collapsing a general-VM search's heap allocations by
         * reserving a block budget up front. Of the handful that remain, counted by size and by symbol,
         * MOST ARE THIS POOL -- its `data`, `refcount` and `free_list` -- and they recur on every call at
         * every subject length, which is what a per-call fixed cost looks like. The rest are the thread
         * lists' `mark`.
         *
         * `pike.hpp` cannot fix this itself: it sits BELOW this header in the layering contract, so its
         * `capture_pool` alias has no `small_vec` to reach for. The pool is not a member of
         * \ref real::detail::basic_pike_state either — each concrete state declares its own — so
         * choosing the container here is the whole change.
         *
         * The inline capacity is the SMALLEST that covers the reserve: `reset` asks for
         * `slot_count * 8`, and a capture-free pattern has `slot_count == 2`, so sixteen values fit a
         * groupless walk exactly. Patterns WITH groups still spill, deliberately — growing this state is
         * what has repeatedly cost gcc/x86 its class-scan codegen (see the `mark` capacity note in the
         * thread list above), and a bigger inline block would trade a measured regression on routes
         * that never build a pool for an allocation on patterns that do.
         */
        basic_capture_pool<small_vec<std::size_t, 16>,
                           small_vec<std::int32_t, 8>,
                           small_vec<std::uint32_t, 8>> pool;
        std::optional<lazy_dfa>                         fwd_dfa;                       //!< Fallback when immut is null; prefer shared_fwd_dfa.
        std::optional<reverse_dfa>                      rev_dfa;                       //!< Fallback reverse; prefer shared_rev_dfa.
        const void                     *                dfa_program         {nullptr}; //!< Program the per-state DFAs were built for (fallback).
        std::optional<reverse_dfa>                      il_prefix_rev;                 //!< Fallback IL prefix reverse; prefer shared_il_prefix_rev.
        const void                     *                il_prefix_for       {nullptr}; //!< Fallback: prefix program il_prefix_rev was built for.
        const void                     *                il_text             {nullptr}; //!< IL: the haystack \ref il_abandoned refers to.
        bool                                            il_abandoned        {false};   //!< IL: a linearity/density guard tripped on this haystack.
        std::uint32_t                                   il_density_cands    {};        //!< IL candidates seen on this haystack.
        std::size_t                                     il_density_origin   {npos};    //!< First IL candidate byte offset this haystack.
        const void                     *                rare_disc_text      {nullptr}; //!< Rare-disc: haystack \ref rare_disc_abandoned refers to.
        bool                                            rare_disc_abandoned {false};   //!< Rare-disc density guard: stay on prefix for this haystack.
        const void                     *                ac_text             {nullptr}; //!< AC: the haystack \ref ac_dense was decided on.
        bool                                            ac_decided          {false};   //!< AC: the density sample has run on this haystack.
        bool                                            ac_dense            {false};   //!< AC: candidates are dense enough that the automaton wins.
        //! \brief This storage benefits from the multi-literal route (\ref pike_vm::ac_ready). A marker,
        //!        not a field: the automaton lives per regex in \ref detail::regex_immutables.
        static constexpr bool             supports_aho_corasick {true};
      };

      std::string     pattern_text;                  //!< The original pattern text.
      dynamic_program program;                       //!< The compiled program.
      flags           effective_flags {flags::none}; //!< Constructor flags merged with any leading `(?imsxaU)` group.

      //! \brief Per-regex lazy-DFA/one-pass cache, built under program-identity invalidation (thread-safe)
      //!        and shared by every search on this regex — not rebuilt per find_iter. `mutable`: a const
      //!        regex fills it on first routed search. Copy/move leave a fresh unbuilt cache; assignment
      //!        invalidates \c built_for so assign-onto-warmed rebuilds (see \ref detail::regex_immutables).
      mutable detail::regex_immutables immut_ {};

      /*!
       * \brief Parses and compiles \p pattern with flags \p compile_flags.
       * \param[in] pattern       The pattern text.
       * \param[in] compile_flags The requested flags (merged with a leading `(?imsxaU)` / `(?flags-flags)` group).
       * \return A populated storage object.
       * \throws real::regex_error on an invalid or over-limit pattern.
       */
      static constexpr dynamic_storage compile(std::string_view pattern,
                                               flags            compile_flags)
      {
        const ast   tree      {detail::parse(pattern, compile_flags)};
        const flags effective {compile_flags | tree.inline_flags};
        // What the COMPILER is handed stays `effective` (added only) -- deliberately not the
        // removal-adjusted set. The parser already applied any `(?-flags)` removal to its own base scope,
        // so every folding and tokenization decision was made under the right flags; narrowing what
        // `compile` sees here would change a behaviour that is already correct. What is REPORTED is the
        // set in force, so the accessor and the engine agree on a global removal.
        dynamic_program prog {detail::compile(tree, effective)};
        // The fixed-shape test seam is applied HERE, once per regex, and NOT in run()'s dispatch gate.
        // It was in that gate first, and the cost is why it moved: this route enters `run()` once per
        // MATCH, where a batched route enters once per batch, so a handful of instructions in the gate is
        // paid per match here and amortised everywhere else. The seven other route seams do sit in gates
        // and cost nothing measurable, for exactly that reason.
        // Clearing the hint takes the route out with no per-match test at all, and `fs_pair_width` goes
        // with it exactly as prefilter.hpp's lookaround wipe pairs them.
        //
        // The AGGREGATE return below is required, not stylistic: a named local returned by value needs
        // dynamic_storage's own constructor, which is not constexpr, and `real::regex` IS used in constant
        // evaluation (tests/engine/test_prefilter.cpp's static_asserts caught exactly that).
        if (!std::is_constant_evaluated() && detail::fixed_shape_route_disabled()) {
          prog.hints.fixed_shape   = false;
          prog.hints.fs_pair_width = 0;
        }
        return {.pattern_text    = std::string(pattern),
                .program         = std::move(prog),
                .effective_flags = flags_without(effective, tree.inline_removed)};
      }

      /*!
       * \brief Returns a non-owning view of the compiled program.
       *
       * \note **Returning by value here is deliberate, and the alternatives are priced.** The
       *       compile-time storage hands back a reference and explains why; this one builds the view per
       *       call, and `view()` runs once per `search()`. Two ways to remove that were prototyped and
       *       refused, each on its own ground:
       *       - *Materialise the view* behind an identity guard, so the construction happens once per
       *         program. It enlarges every regex object by the size of a view, which is the wrong
       *         direction for anyone holding many patterns, and the guard is subtler than it looks: a
       *         MOVE leaves `program.code.data()` unchanged, so a guard testing only the program's
       *         identity validates a view still pointing into the moved-from object. The obvious
       *         one-condition form aborts the lifetime tests as a double free; a correct guard must also
       *         test `view_.immut != &immut_`.
       *       - *Carry `pattern_hints` by pointer* instead of copying it into the view. This shrinks the
       *         construction rather than removing it, so it can win at most a fraction of what removing
       *         it wins -- against an indirection on every hot hint read, at every site that reads one.
       *
       *       The whole prize is a few nanoseconds per search, and both standing proposals were competing
       *       for that same budget. The dynamic path's real gap to the compile-time one is several times
       *       larger than the prize, so most of it is somewhere else entirely.
       *
       * \return The view; valid as long as this storage is alive.
       */
      [[nodiscard]] constexpr program_view view() const
      {
        program_view pv {program.view()};
        pv.immut = &immut_; // the router builds/uses the per-regex cache through the view
        return pv;
      }

      /*!
       * \brief Returns the original pattern text.
       * \return The pattern, valid as long as this storage is alive.
       */
      [[nodiscard]] constexpr std::string_view pattern() const
      {
        return pattern_text;
      }

      /*!
       * \brief Returns the flag set in force: constructor flags, plus a leading global-flags group's
       *        additions, minus its `-removal` -- see \ref real::basic_regex::compile_flags.
       * \return The effective flag set.
       */
      [[nodiscard]] constexpr flags compiled_flags() const
      {
        return effective_flags;
      }
    };

    /*!
     * \brief IL: the per-haystack guard fields the inner-literal route needs, for a compile-time storage.
     *
     * Scalars only — no `il_prefix_rev`, because that storage has no per-regex immutables and so never
     * builds a reverse DFA: the reverse-confirm sub-case declines and hands back to the core VM, while a
     * candidate-free (no-match) sweep stays on memmem. Their presence in the scratch type is also what
     * admits the storage to the route at all (the `requires` gate in \ref pike_vm::run).
     */
    struct static_il_guard_fields
    {
      const void*   il_text           {nullptr}; //!< IL: the haystack \ref il_abandoned refers to.
      bool          il_abandoned      {false};   //!< IL: a guard tripped on this haystack — stay on the core.
      std::uint32_t il_density_cands  {};        //!< IL candidates seen on this haystack.
      std::size_t   il_density_origin {npos};    //!< First IL candidate byte offset this haystack.
    };

    /*! \brief No IL fields: the route is not compiled for this pattern. */
    struct static_no_il_guard_fields
    {};

    /*!
     * \brief Rounds a program length up to the scratch capacity tier it shares with its neighbours.
     *
     * Sharing \ref static_pike_scratch is by exact template arguments, so keying it on the exact
     * `code_size` means two patterns one instruction apart still instantiate every Pike VM route twice.
     * Rounding to powers of two collapses neighbours onto one type while keeping the over-allocation
     * bounded by a factor of two, where a coarse ladder would multiply the smallest patterns' scratch
     * several times over. The floor of 8 keeps the ladder from splintering at the bottom, where the
     * patterns are densest and the absolute sizes smallest.
     *
     * Rounding UP only: every capacity derived from this is a bound the exact size must not exceed, so a
     * tier is always safe where the measured value was.
     *
     * \param[in] code_size The program's exact instruction count.
     * \return The tier capacity, a power of two and at least 8.
     */
    [[nodiscard]] constexpr std::size_t scratch_code_tier(std::size_t code_size)
    {
      std::size_t tier {8};
      while (tier < code_size) {
        tier <<= 1U;
      }
      return tier;
    }

    /*!
     * \brief Compile-time-storage VM scratch, all fixed-capacity (zero heap), keyed on DIMENSIONS ONLY.
     *
     * The epsilon DFS stack is bounded because each pc is processed once and pushes at most two explore
     * entries plus one restore entry. Capture slots live in a copy-on-write \ref basic_capture_pool (a
     * thread carries one block index, not a slot run), sized for the worst-case block count — the same
     * zero-heap, compile-sized discipline.
     *
     * \note Deliberately parameterised on sizes rather than on the pattern, so two patterns of the same
     *       shape can share one instantiation. Anything that depends on the pattern's *value* belongs in
     *       the thin per-pattern type deriving from this, not here — see \ref g_inlinebudget for what the
     *       distinction costs when it is not maintained.
     *
     * \tparam CodeSize  Scratch capacity in instructions — a TIER (see \ref scratch_code_tier), not the
     *                   exact program length, so neighbouring shapes share one instantiation.
     * \tparam SlotCount Capture slots.
     * \tparam WantsIL   Whether the inner-literal route is compiled in.
     */
    template <std::size_t CodeSize, std::size_t SlotCount, bool WantsIL>
    struct static_pike_scratch : basic_pike_state<
                                   basic_thread_list<static_vec<std::int32_t, CodeSize>,
                                                     static_vec<std::size_t, CodeSize>,
                                                     static_vec<std::uint64_t, CodeSize>>,
                                   static_vec<eps_entry, (3 * CodeSize) + 4>>,
                                 std::conditional_t<WantsIL, static_il_guard_fields, static_no_il_guard_fields>
    {
      //! \brief Worst-case live capture blocks: every reference (a DFS stack frame or a thread in either
      //!        list) could point at a distinct block, and the stack is `(3*CodeSize)+4` with each list
      //!        holding up to `CodeSize` threads. Freed blocks recycle through the pool's free list, so
      //!        the pool never grows past this. Derived from `CodeSize` HERE rather than passed in, so it
      //!        cannot disagree with the tier: a bound computed from the exact program length would vary
      //!        between two patterns sharing a tier and split them back into separate types.
      static constexpr std::size_t max_blocks {(5 * CodeSize) + 8};

      basic_capture_pool<static_vec<std::size_t, max_blocks * SlotCount>,
                         static_vec<std::int32_t, max_blocks>,
                         static_vec<std::uint32_t, max_blocks>> pool; //!< COW capture blocks (zero heap).
    };

    /*!
     * \brief Storage policy backing `real::static_regex`: compile-time, stateless.
     *
     * Every array is a `static` `constexpr` member sized exactly by a measuring
     * pass over the same compilation, so a `static_regex` object is stateless
     * (`sizeof` 1) and matching allocates nothing.
     *
     * \tparam Pat The pattern, as a \ref real::fixed_string non-type parameter.
     * \tparam F   Compilation flags.
     */
    template <fixed_string Pat, flags F = flags::none>
    struct static_storage
    {
      static constexpr bool is_compile_time {true}; //!< Selects the default constructor.
      /*!
       * \brief Name-resolution owner: empty, the tables having static storage duration.
       */
      using name_owner = borrowed_names;

    private:

      /*!
       * \brief Returns the freshly built program (used for both measuring and filling).
       *
       * Runs only at compile time (a `static_regex` instantiation), so it is invisible to the
       * runtime coverage report; it is exercised by the constexpr `static_assert`s in
       * tests/test_static.cpp and tests/test_constexpr.cpp.
       *
       * \return The compiled program.
       */
      static constexpr dynamic_program build()
      {
        const ast       tree {detail::parse(Pat.view(), F)};
        dynamic_program prog {detail::compile(tree, F | tree.inline_flags)};
        if (!prog.lookarounds.empty()) {
          // Honest absence: there is no constexpr sub-VM to evaluate a lookaround. A clear compile
          // error -- this throw, evaluated at compile time -- beats a silent miscompile.
          throw regex_error("static_regex does not support lookarounds yet (use real::regex)", 0);
        }
        return prog;
      }

      /*!
       * \brief Copies the first \p N elements of \p v into a fixed array.
       * \tparam T   Element type.
       * \tparam N   Exact size (measured from \ref build).
       * \tparam Vec Source container type.
       * \param[in] source The source vector.
       * \return The exactly-sized array.
       */
      template <typename T, std::size_t N, typename Vec>
      static constexpr std::array<T, N> take(const Vec& source)
      {
        std::array<T, N> result {};
        for (std::size_t i = 0; i < N; ++i) {
          result[i] = source[i];
        }
        return result;
      }

    public:

      /*!
       * \brief Everything \ref build yields that is NOT a range, measured in ONE evaluation.
       *
       * The two-phase this class is built on -- measure, then freeze, because C++20 forbids persistent
       * constexpr allocation -- does not say how many times to measure, and the measuring had grown to
       * SEVEN separate `build()` calls: one per size, one per scalar, each its own top-level constant
       * expression re-running the whole compiler front end. Collapsed here to one, halving the number of
       * `build()` evaluations the class costs (the five range members below are the rest).
       *
       * That is a compile-time change with no run-time surface: the frozen arrays are byte-identical,
       * because they are produced by the same \ref take calls from the same builder. What it buys is
       * headroom against the per-expression constexpr step budget -- the ceiling \ref class_tables below
       * is already annotated for, and the reason `build_byte_program` reaches only byte classes.
       */
      struct measured
      {
        pattern_hints hints          {}; //!< Search hints.
        std::size_t   code_size      {}; //!< Instruction count.
        std::size_t   class_count    {}; //!< Distinct byte-class count.
        std::size_t   name_count     {}; //!< Named-group count.
        std::size_t   cp_class_count {}; //!< Code-point class count (`klass_cp`).
        std::size_t   cp_range_count {}; //!< Total code-point ranges.
        std::uint16_t slot_count     {}; //!< `2*(groups+1)`.
      };

      /*!
       * \brief Whether \ref build is a constant expression for this pattern.
       *
       * A `requires` expression is a SFINAE context: forming the `bool_constant` template argument
       * needs a constant expression, and \ref build throwing makes it one substitution failure
       * rather than a hard error. That is what lets \ref viable be *asked* instead of crashed into.
       */
      static constexpr bool viable {requires { typename std::bool_constant<(build(), true)>; }};

      // Placed BEFORE every member that reads `survey`: the failing assertion ends this
      // instantiation, so the twenty-odd dependent members below never each report their own
      // "must be initialized by a constant expression" (measured: 358 diagnostic lines for one
      // backreference, the reason buried at line 39, against 22 with this assertion in front).
      static_assert(viable,
                    "real::static_regex: this pattern cannot be compiled at compile time -- a "
                    "backreference, a POSIX class, or a lookaround (static_regex has no constexpr "
                    "sub-VM for one). Compile the same pattern as a runtime real::regex to get the "
                    "exact construct and its position, or rewrite it.");

      //! \brief The one measuring evaluation. Never emitted: nothing takes its address, and every
      //!        member below reads it at compile time only.
      static constexpr measured survey {[] {
                                          const dynamic_program p {build()};
                                          return measured {.hints          = p.hints,
                                                           .code_size      = p.code.size(),
                                                           .class_count    = p.classes.size(),
                                                           .name_count     = p.names.size(),
                                                           .cp_class_count = p.cp_classes.size(),
                                                           .cp_range_count = p.cp_ranges.size(),
                                                           .slot_count     = p.slot_count};
                                        }()};

      //! \brief The flag set in force: \c F plus what a leading `(?imsxaU)` group added, minus what a
      //!        `(?flags-flags)` removal cleared. Mirrors \ref dynamic_storage::compile, so the two
      //!        storages report the same thing for the same pattern. Parsed ONCE -- the two-call form
      //!        this replaces ran the parser twice for one answer, the same defect as the seven-call
      //!        measure above in miniature.
      static constexpr flags         effective_flags            {[] {
                                                                   const auto parsed {detail::parse(Pat.view(), F)};
                                                                   return flags_without(F | parsed.inline_flags,
                                                                                        parsed.inline_removed);
                                                                 }()};
      static constexpr pattern_hints hints                      {survey.hints};                                  //!< Search hints.
      static constexpr std::size_t   code_size                  {survey.code_size};                              //!< Instruction count.
      static constexpr std::size_t   class_count                {survey.class_count};                            //!< Distinct class count.
      static constexpr std::size_t   name_count                 {survey.name_count};                             //!< Named-group count.
      static constexpr std::size_t   cp_class_count             {survey.cp_class_count};                         //!< Code-point class count (klass_cp).
      static constexpr std::size_t   cp_range_count             {survey.cp_range_count};                         //!< Total code-point ranges.
      static constexpr std::uint16_t slot_count                 {survey.slot_count};                             //!< `2*(groups+1)`.

      static constexpr std::array<instr, code_size>        code {take<instr, code_size>(build().code)};          //!< The program.
      static constexpr std::array<char_class, class_count> classes =
        take<char_class, class_count>(build().classes);                                                          //!< Interned classes.
      static constexpr std::array<named_group, name_count> names =
        take<named_group, name_count>(build().names);                                                            //!< Named groups.
      static constexpr std::array<cp_class, cp_class_count> cp_classes =
        take<cp_class, cp_class_count>(build().cp_classes);                                                      //!< Code-point classes.
      static constexpr std::array<code_range, cp_range_count> cp_ranges =
        take<code_range, cp_range_count>(build().cp_ranges);                                                     //!< Flat range buffer.

      //! \brief Flat byte-class membership tables, built at compile time: `class_tables[i*256 + b]`.
      //!        Reuses the \ref classes member rather than calling \ref build again — an extra `build()` per
      //!        table pushes the whole instantiation past clang's constexpr step budget.
      //!
      //! \note **A pack-expansion `tabulate<N>(f)` here was written, measured and REFUSED.** The loop
      //!       below cannot be one pass: constant evaluation rejects indeterminate subobjects, so the
      //!       array is zeroed and then overwritten — 2N element operations where a pack expansion needs
      //!       N. The argument is sound and buys nothing. Compile time is indistinguishable between the
      //!       two forms, with the direction flipping between paired runs; and bisecting
      //!       `-fconstexpr-steps` to the failure point gives the SAME budget for both, so there is no
      //!       headroom in it either — the compiler's own cost for a large pack cancels the halved
      //!       element count. The loop stays: same speed, same budget, no helper to maintain.
      static constexpr std::array < std::uint8_t, (class_count == 0 ? 1 : class_count) * 256 > class_tables {[] {
                                                                                                               std::array < std::uint8_t, (class_count == 0 ? 1 : class_count) * 256 > t {};
                                                                                                               for (std::size_t i = 0; i < class_count; ++i) {
                                                                                                                 for (std::size_t b = 0; b < 256; ++b) {
                                                                                                                   t[(i * 256) + b] = classes[i].test(static_cast<std::uint8_t>(b)) ? std::uint8_t {1} : std::uint8_t {0};
                                                                                                                 }
                                                                                                               }
                                                                                                               return t;
                                                                                                             }()};

      //! \brief Flat ASCII tables for the code-point classes: `cp_ascii_tables[i*256 + b]`.
      static constexpr std::array < std::uint8_t, (cp_class_count == 0 ? 1 : cp_class_count) * 256 >
      cp_ascii_tables {[] {
                         std::array < std::uint8_t, (cp_class_count == 0 ? 1 : cp_class_count) * 256 > t {};
                         for (std::size_t i = 0; i < cp_class_count; ++i) {
                           for (std::size_t b = 0; b < 256; ++b) {
                             t[(i * 256) + b] = cp_classes[i].ascii.test(static_cast<std::uint8_t>(b)) ? std::uint8_t {1}
                                                                                       : std::uint8_t {0};
                           }
                         }
                         return t;
                       }()};

      //! \brief Two-byte-range membership bitmaps (U+0080..U+07FF) for the code-point classes, 30 words each.
      static constexpr std::array < std::uint64_t, (cp_class_count == 0 ? 1 : cp_class_count) * 30 >
      cp_page_tables {[] {
                        std::array < std::uint64_t, (cp_class_count == 0 ? 1 : cp_class_count) * 30 > t {};
                        for (std::size_t i = 0; i < cp_class_count; ++i) {
                          for (std::uint32_t k = 0; k < cp_classes[i].range_count; ++k) {
                            const code_range& r {cp_ranges[cp_classes[i].range_begin + k]};
                            if (r.lo > 0x7FFU) {
                              break; // sorted: nothing more falls in the page
                            }
                            const std::uint32_t lo {r.lo < 0x80U ? 0x80U : r.lo};
                            const std::uint32_t hi {r.hi > 0x7FFU ? 0x7FFU : r.hi};
                            for (std::uint32_t c = lo; c <= hi; ++c) {
                              const std::uint32_t bit {c - 0x80U};
                              t[(i * 30) + (bit >> 6U)] |= std::uint64_t {1} << (bit & 63U);
                            }
                          }
                        }
                        return t;
                      }()};

      //! \brief Capture-slot storage, sized exactly to the program's slot count (no heap).
      using slot_storage = static_vec<std::size_t, slot_count>;
      //! \brief IL guard fields for this storage — lifted to \ref static_il_guard_fields, which carries
      //!        the rationale; kept as a name here because \ref wants_inner_literal reads next to it.
      using il_guard_fields = static_il_guard_fields;

      //! \brief No IL fields: the route is not compiled for this pattern (see \ref wants_inner_literal).
      using no_il_guard_fields = static_no_il_guard_fields;

      /*!
       * \brief Whether the inner-literal route is worth compiling into this pattern's `run()`.
       *
       * A required literal at offset >= 1 is necessary but not sufficient. `fixed_shape` means the core
       * already has an arithmetic-width scan for the whole pattern, and then memmem has nothing to add;
       * without it the core falls to the general VM, which is what the literal sweep rescues. The shape
       * of the result is the same on every pattern measured: a non-fixed-shape pattern gains by orders of
       * magnitude on a subject with NO match -- where the literal scan rejects the whole corpus and the
       * general VM would walk it -- and is neutral once matches are dense enough that the scan finds one
       * immediately. A fixed_shape pattern gains nothing and pays for the attempt.
       *
       * Excluding it HERE rather than at run time is what keeps the cost off the patterns that do not use
       * the route: compiling the block into `run()` at all is measurable on a pattern with no inner
       * literal, which never enters it.
       */
      static constexpr bool wants_inner_literal {hints.inner_literal_len > 0 && hints.inner_literal_prefix >= 1
                                                 && !hints.fixed_shape};

      /*!
       * \brief This pattern's VM scratch — nothing but \ref static_pike_scratch at this pattern's
       *        dimensions, so two patterns of the same shape name the same type.
       *
       * Nothing here depends on the pattern's *value* any more. The three byte-class table addresses that
       * used to live in this type now travel in \ref real::detail::program_view, which is where runtime
       * program data belongs; they remain `static constexpr` arrays, so a constant-folding compiler still
       * reaches them without a load. What that buys, and what it cost to establish, is in
       * \ref g_inlinebudget.
       */
      using state_type = static_pike_scratch<scratch_code_tier (code_size), slot_count, wants_inner_literal>;

      /*!
       * \brief Returns a non-owning view of the compile-time program, by reference.
       *
       * Every field is a compile-time constant here — the spans point at `static constexpr` arrays and
       * `immut` is null — so the whole view is one too, and handing back a reference costs nothing where
       * returning by value copied the whole thing per call. 232 of its 432 bytes are \ref pattern_hints,
       * and `view()` is called once per `search()`: line-level profiling of a single `[a-z]+` search put
       * that one aggregate initialiser at 93 of the ~325 instructions the call spends, against 17 for the
       * class scan itself. The measurement was taken at 408 bytes, before the three table bases moved into
       * the view; the dynamic storage still returns by value and so still pays a construction of that size
       * once per search — the one place this reasoning has not been applied.
       *
       * \return A reference to the single compile-time view; it outlives every caller.
       */
      [[nodiscard]] constexpr const program_view& view() const
      {
        return view_;
      }

    private:

      //! \brief The view itself, materialised once at compile time. See \ref view.
      static constexpr program_view view_ {.code = code,
                                           .classes           = classes,
                                           .names             = names,
                                           .lookarounds       = {}, // static_regex rejects lookarounds at compile (always empty)
                                           .cp_classes        = cp_classes,
                                           .cp_ranges         = cp_ranges,
                                                                    // IL: no prefix sub-program. This storage never runs the reverse confirm (that needs the
                                                                    // per-regex immutables it has none of), and a second compile inside a constant expression
                                                                    // is what the budget cannot afford -- it pushed tests/frontend/test_constexpr.cpp's
                                                                    // flag_cases() past clang's step limit. See \ref wants_inner_literal.
                                           .prefix_code       = {},
                                           .prefix_classes    = {},
                                           .prefix_cp_classes = {},
                                           .prefix_cp_ranges  = {},
                                           .slot_count        = slot_count,
                                           .byte_mode         = has_flag(effective_flags, flags::bytes),
                                           .unicode_word      = !has_flag(effective_flags, flags::bytes) && !has_flag(effective_flags, flags::ascii),
                                           .hints             = hints,
                                           .immut             = nullptr,
                                           // The three table bases travel in the VIEW, not in the state type: a state
                                           // naming this pattern's arrays cannot be shared, and every route is then
                                           // instantiated per pattern. These stay `static constexpr` addresses, so a
                                           // constant-folding compiler still reaches them without a load.
                                           .class_tables      = class_tables.data(),
                                           .cp_ascii_tables   = cp_ascii_tables.data(),
                                           .cp_page_tables    = cp_page_tables.data()};

    public:

      /*!
       * \brief Returns the pattern text.
       * \return A view of the compile-time pattern string.
       */
      [[nodiscard]] constexpr std::string_view pattern() const
      {
        return Pat.view();
      }

      /*!
       * \brief Returns the flag set in force: constructor flags, plus a leading global-flags group's
       *        additions, minus its `-removal` -- see \ref real::basic_regex::compile_flags.
       * \return The effective flag set.
       */
      [[nodiscard]] constexpr flags compiled_flags() const
      {
        return effective_flags;
      }
    };
  } // namespace detail
} // namespace real

#endif // REAL_STORAGE_HPP
