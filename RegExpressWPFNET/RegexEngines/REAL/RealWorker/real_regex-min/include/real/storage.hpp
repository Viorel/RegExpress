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

#include "version.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "compiler.hpp"
#include "pike.hpp"
#include "program.hpp"

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
     */
    [[nodiscard]] constexpr std::string_view view() const
    {
      return {data, N - 1};
    }
  };

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
       * \brief Returns the number of elements.
       */
      [[nodiscard]] constexpr std::size_t size() const
      {
        return size_;
      }

      /*!
       * \brief Returns `true` if empty.
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

      std::array<T, Cap> data_ {}; //!< Inline element storage.
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
     *          write-before-read order, or it reads indeterminate memory — a silent UB the former
     *          value-init used to mask. MemorySanitizer is the detector (the CI sanitize leg is
     *          ASan/UBSan, which does not catch it); run MSan on the devbox when changing how
     *          small_vec accesses its elements.
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
       * \brief Active member (inline buffer or heap pointer) per \ref is_heap_ state.
       */
      //! \brief Inline element block. A struct (not a bare C array) so the union ctor can
      //!        activate it as a whole with \c construct_at in a constant expression, while
      //!        \ref inline_data still indexes a plain C array — which the static analyzer can
      //!        bound (a \c std::array's `operator[]` hides the extent and trips a false
      //!        out-of-bounds on \ref transfer_range).
      struct inline_block
      {
        T elems[InlineCapacity];
      };
      union Storage
      {
        inline_block inline_buffer; //!< Inline storage (when not heap).
        T*           heap_ptr;      //!< Heap storage (when \ref is_heap_).

        //! \brief Starts in the inline state.
        //!
        //! At **run time** the inline buffer is left UNINITIALIZED: small_vec writes every
        //! element through \c std::construct_at (placement-new) before any read (push_back and
        //! assign), so the value-init was pure overhead — ~30 % of the instruction count on a
        //! findall tokenizing workload (a fresh slot buffer per match, of which ~2 slots serve).
        //! At **compile time** the member must be active and initialized for the constexpr
        //! matching path (which assigns through \ref inline_data), so it is value-initialized
        //! there via \c construct_at on the whole \ref inline_block — activating a class-type
        //! member is what a constexpr union allows (an element-wise or bare C-array activation is not).
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
      } storage_ {};

      // Run-time cache of the active storage base (inline buffer or heap block), refreshed on
      // every state change via \ref refresh_data. The hot accessors (operator[], back, push_back)
      // index through it, so they avoid the per-access `is_heap_` branch that the profile showed
      // dominating add_thread. NOT used during constant evaluation: a pointer to a subobject of
      // *this is not a usable constant across copies, so the constexpr path keeps the is_heap_
      // branch (guarded by std::is_constant_evaluated). static_regex uses static_vec, not
      // small_vec, so this never participates in compile-time matching.
      T* data_ {};

      //! \brief Refreshes \ref data_ to the active storage base (run time only).
      constexpr void refresh_data() noexcept
      {
        if (!std::is_constant_evaluated()) {
          data_ = is_heap_ ? storage_.heap_ptr : inline_data();
        }
      }

      /*!
       * \brief Returns pointer to the inline buffer.
       */
      [[nodiscard]] constexpr T* inline_data() noexcept
      {
        return &storage_.inline_buffer.elems[0];
      }

      /*!
       * \brief Returns const pointer to the inline buffer.
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
       * \brief Frees the heap block, if any (run-time only). \p T is trivially
       *        destructible (see the class `static_assert`), so no element destructors
       *        run — and inline storage needs no cleanup at all.
       */
      constexpr void cleanup() noexcept
      {
        if (std::is_constant_evaluated()) {
          return;
        }
        if (is_heap_) {
          ::operator delete(storage_.heap_ptr);
        }
      }

      /*!
       * \brief Doubles the capacity (saturating), spilling to the heap as needed.
       */
      void extend_capacity()
      {
        const std::size_t current {capacity_};
        const std::size_t new_cap {(current > (std::size_t)-1 / 2) ? (std::size_t)-1 : current * 2};
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
        if (!std::is_constant_evaluated()) {
          cleanup();
        }
      }

      /*!
       * \brief Appends \p value, growing to the heap if the inline buffer is full.
       * \param[in] value The element to append.
       * \throws std::bad_alloc during constant evaluation if growth is needed
       *         (constexpr use must stay within `InlineCapacity`).
       */
      constexpr void push_back(const T& value)
      {
        if (size_ >= capacity_) {
          if (std::is_constant_evaluated()) {
            throw std::bad_alloc {};
          }
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
       * \throws std::bad_alloc during constant evaluation if growth is needed.
       */
      constexpr void assign(std::size_t count,
                            const T&    value)
      {
        clear();
        if (count > capacity_) {
          if (std::is_constant_evaluated()) {
            throw std::bad_alloc {};
          }
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
       * \brief Returns the number of elements.
       */
      [[nodiscard]] constexpr std::size_t size() const noexcept
      {
        return size_;
      }

      /*!
       * \brief Returns `true` if empty.
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
       * \throws std::bad_alloc during constant evaluation (constexpr stays inline).
       */
      constexpr void reserve(std::size_t new_capacity)
      {
        if (new_capacity <= capacity_) {
          return;
        }
        if (std::is_constant_evaluated()) {
          throw std::bad_alloc {};
        }
        T      * new_data {static_cast<T*>(::operator new(new_capacity * sizeof(T)))};
        const T* old_data {is_heap_ ? storage_.heap_ptr : inline_data()};
        transfer_range<false>(old_data, size_, new_data);
        if (is_heap_) {
          ::operator delete(storage_.heap_ptr);
        }
        storage_.heap_ptr = new_data;
        capacity_         = new_capacity;
        is_heap_          = true;
        refresh_data(); // run-time path only (constexpr threw above); data_ now points at the heap block
      }

      /*!
       * \brief Move constructor: steals \p other's heap block or moves inline elements.
       */
      constexpr small_vec(small_vec&& other) noexcept
        : size_(other.size_),
          capacity_(other.capacity_),
          is_heap_(other.is_heap_)
      {
        if (is_heap_) {
          storage_.heap_ptr       = other.storage_.heap_ptr;
          other.storage_.heap_ptr = nullptr;
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
          cleanup();
          size_     = other.size_;
          capacity_ = other.capacity_;
          is_heap_  = other.is_heap_;
          if (is_heap_) {
            storage_.heap_ptr       = other.storage_.heap_ptr;
            other.storage_.heap_ptr = nullptr;
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
        return *this;
      }

      /*!
       * \brief Copy constructor (needed for `vector<match_result>` in find_all).
       */
      constexpr small_vec(const small_vec& other)
        : size_(other.size_),
          capacity_(other.capacity_)
      {
        if (other.is_heap_) {
          if (std::is_constant_evaluated()) {
            throw std::bad_alloc {}; // dynamic heap path not for constexpr (static_regex uses static_vec)
          }
          storage_.heap_ptr = static_cast<T*>(::operator new(other.capacity_ * sizeof(T)));
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
            storage_.heap_ptr = static_cast<T*>(::operator new(other.capacity_ * sizeof(T)));
            transfer_range<false>(other.storage_.heap_ptr, other.size_, storage_.heap_ptr);
            is_heap_  = true;
            capacity_ = other.capacity_;
          }
          else {
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
     * \brief Storage policy backing `real::regex`: heap, sized once at run time.
     *
     * Match scratch uses small-buffer-optimized containers, so the common
     * small-group match runs without a heap allocation.
     */
    struct dynamic_storage
    {
      static constexpr bool is_compile_time {}; //!< Selects the runtime constructor.
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
                                              std::vector<std::uint64_t>>,
                            small_vec<std::size_t, 64>,
                            small_vec<eps_entry, 32>>
      {
        lookaround_scratch lookaround; //!< Isolated sub-scratch for bounded lookaround evaluation.
      };

      std::string     pattern_text;                  //!< The original pattern text.
      dynamic_program program;                       //!< The compiled program.
      flags           effective_flags {flags::none}; //!< Constructor flags merged with any (?ims).

      /*!
       * \brief Parses and compiles \p pattern with flags \p compile_flags.
       * \param[in] pattern       The pattern text.
       * \param[in] compile_flags The requested flags (merged with a leading (?ims)).
       * \return A populated storage object.
       * \throws real::regex_error on an invalid or over-limit pattern.
       */
      static constexpr dynamic_storage compile(std::string_view pattern,
                                               flags            compile_flags)
      {
        const ast   tree      {detail::parse(pattern, compile_flags)};
        const flags effective {compile_flags | tree.inline_flags};
        return {.pattern_text    = std::string(pattern),
                .program         = detail::compile(tree, effective),
                .effective_flags = effective};
      }

      /*!
       * \brief Returns a non-owning view of the compiled program.
       */
      [[nodiscard]] constexpr program_view view() const
      {
        return program.view();
      }

      /*!
       * \brief Returns the original pattern text.
       */
      [[nodiscard]] constexpr std::string_view pattern() const
      {
        return pattern_text;
      }

      /*!
       * \brief Returns the effective flags (constructor flags merged with (?ims)).
       */
      [[nodiscard]] constexpr flags compiled_flags() const
      {
        return effective_flags;
      }
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

    private:

      /*!
       * \brief Returns the freshly built program (used for both measuring and filling).
       *
       * Runs only at compile time (a `static_regex` instantiation), so it is invisible to the
       * runtime coverage report; it is exercised by the constexpr `static_assert`s in
       * tests/test_static.cpp and tests/test_constexpr.cpp.
       */
      static constexpr dynamic_program build()
      {
        const ast       tree {detail::parse(Pat.view(), F)};
        dynamic_program prog {detail::compile(tree, F | tree.inline_flags)};
        if (!prog.lookarounds.empty()) {
          // Honest absence: the constexpr sub-VM is a measured follow-up. A clear compile
          // error (this throw, evaluated at compile time) beats a silent miscompile.
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

      static constexpr flags         effective_flags            {F | detail::parse(Pat.view(), F).inline_flags}; //!< Flags merged with (?ims).
      static constexpr pattern_hints hints                      {build().hints};                                 //!< Search hints.
      static constexpr std::size_t   code_size                  {build().code.size()};                           //!< Instruction count.
      static constexpr std::size_t   class_count                {build().classes.size()};                        //!< Distinct class count.
      static constexpr std::size_t   name_count                 {build().names.size()};                          //!< Named-group count.
      static constexpr std::uint16_t slot_count                 {build().slot_count};                            //!< `2*(groups+1)`.

      static constexpr std::array<instr, code_size>        code {take<instr, code_size>(build().code)};          //!< The program.
      static constexpr std::array<char_class, class_count> classes =
        take<char_class, class_count>(build().classes);                                                          //!< Interned classes.
      static constexpr std::array<named_group, name_count> names =
        take<named_group, name_count>(build().names);                                                            //!< Named groups.

      /*!
       * \brief Capture-slot container: fixed-capacity, no heap.
       */
      using slot_storage = static_vec<std::size_t, slot_count>;
      /*!
       * \brief VM scratch state, all fixed-capacity (zero heap).
       *
       * The epsilon DFS stack is bounded because each pc is processed once and
       * pushes at most two explore entries plus one restore entry.
       */
      using state_type = basic_pike_state<
        basic_thread_list<static_vec<std::int32_t, code_size>,
                          static_vec<std::size_t, code_size * slot_count>,
                          static_vec<std::uint64_t, code_size>>,
        static_vec<std::size_t, slot_count>,
        static_vec<eps_entry, (3 * code_size) + 4>>;

      /*!
       * \brief Returns a non-owning view of the compile-time program.
       */
      [[nodiscard]] constexpr program_view view() const
      {
        return {.code        = code,
                .classes     = classes,
                .names       = names,
                .lookarounds = {}, // static_regex rejects lookarounds at compile (always empty)
                .slot_count  = slot_count,
                .byte_mode   = has_flag(effective_flags, flags::bytes),
                .hints       = hints};
      }

      /*!
       * \brief Returns the pattern text.
       */
      [[nodiscard]] constexpr std::string_view pattern() const
      {
        return Pat.view();
      }

      /*!
       * \brief Returns the effective flags.
       */
      [[nodiscard]] constexpr flags compiled_flags() const
      {
        return effective_flags;
      }
    };
  } // namespace detail
} // namespace real

#endif // REAL_STORAGE_HPP
