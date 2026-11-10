module fregex_charset
!! Sets of byte values (0-255), used for character classes such as `[a-z]` and `\w`.
use iso_fortran_env, only: int64
implicit none
private

public :: digit_set, word_set, space_set, full_set

type, public :: charset_t
    !! A 256-bit set of byte values.
    integer(int64) :: bits(0:3) = 0_int64
contains
    procedure :: add => charset_add
    procedure :: add_range => charset_add_range
    procedure :: add_set => charset_add_set
    procedure :: has => charset_has
    procedure :: negate => charset_negate
    procedure :: fold_case => charset_fold_case
    procedure :: table => charset_table
end type charset_t

contains

pure subroutine charset_add(self, c)
    class(charset_t), intent(inout) :: self
    integer, intent(in) :: c
    if (c < 0 .or. c > 255) return
    self%bits(c/64) = ibset(self%bits(c/64), mod(c, 64))
end subroutine charset_add

pure subroutine charset_add_range(self, lo, hi)
    class(charset_t), intent(inout) :: self
    integer, intent(in) :: lo, hi
    integer :: c
    do c = lo, hi
        call self%add(c)
    end do
end subroutine charset_add_range

pure subroutine charset_add_set(self, other)
    class(charset_t), intent(inout) :: self
    type(charset_t), intent(in) :: other
    self%bits = ior(self%bits, other%bits)
end subroutine charset_add_set

pure logical function charset_has(self, c) result(has)
    !! True if byte `c` is in the set. Out-of-range values (e.g. -1 for "end of string") are never members.
    class(charset_t), intent(in) :: self
    integer, intent(in) :: c
    if (c < 0 .or. c > 255) then
        has = .false.
    else
        has = btest(self%bits(c/64), mod(c, 64))
    end if
end function charset_has

pure function charset_table(self) result(table)
    !! The set as a lookup table indexed by byte value. Index -1 ("end of string") is never a member.
    class(charset_t), intent(in) :: self
    logical :: table(-1:255)
    integer :: c
    table(-1) = .false.
    do c = 0, 255
        table(c) = btest(self%bits(c/64), mod(c, 64))
    end do
end function charset_table

pure subroutine charset_negate(self)
    class(charset_t), intent(inout) :: self
    self%bits = not(self%bits)
end subroutine charset_negate

pure subroutine charset_fold_case(self)
    !! Add the other ASCII case of every letter in the set.
    class(charset_t), intent(inout) :: self
    integer :: c
    do c = iachar('a'), iachar('z')
        if (self%has(c) .or. self%has(c - 32)) then
            call self%add(c)
            call self%add(c - 32)
        end if
    end do
end subroutine charset_fold_case

pure function digit_set() result(set)
    !! `\d`: `[0-9]`
    type(charset_t) :: set
    call set%add_range(iachar('0'), iachar('9'))
end function digit_set

pure function word_set() result(set)
    !! `\w`: `[A-Za-z0-9_]`
    type(charset_t) :: set
    call set%add_range(iachar('0'), iachar('9'))
    call set%add_range(iachar('A'), iachar('Z'))
    call set%add_range(iachar('a'), iachar('z'))
    call set%add(iachar('_'))
end function word_set

pure function space_set() result(set)
    !! `\s`: `[ \t\n\r\f\v]`
    type(charset_t) :: set
    call set%add_range(9, 13)
    call set%add(iachar(' '))
end function space_set

pure function full_set() result(set)
    type(charset_t) :: set
    set%bits = not(0_int64)
end function full_set

end module fregex_charset
