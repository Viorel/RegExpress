module fregex_string
!! String helpers shared by the fregex modules.
implicit none
private

public :: string_builder_t
public :: byte_at, to_lower_code, is_word_code, is_alpha_code, is_digit_code, to_str

type, public :: regex_string_t
    !! A variable-length string, so that arrays of strings can hold different lengths.
    character(len=:), allocatable :: s
end type regex_string_t

type :: string_builder_t
    !! Builds a string by appending pieces in amortized constant time.
    private
    character(len=:), allocatable :: buf
    integer :: n = 0
contains
    procedure :: append => builder_append
    procedure :: get => builder_get
    procedure :: clear => builder_clear
end type string_builder_t

contains

pure integer function byte_at(s, i) result(c)
    !! Byte value (0-255) of `s(i:i)`, or -1 if `i` is outside the string.
    character(len=*), intent(in) :: s
    integer, intent(in) :: i
    if (i < 1 .or. i > len(s)) then
        c = -1
    else
        c = iand(ichar(s(i:i)), 255)
    end if
end function byte_at

elemental integer function to_lower_code(c) result(l)
    !! ASCII lowercase of a byte value; other values are returned unchanged.
    integer, intent(in) :: c
    if (c >= iachar('A') .and. c <= iachar('Z')) then
        l = c + 32
    else
        l = c
    end if
end function to_lower_code

elemental logical function is_alpha_code(c)
    integer, intent(in) :: c
    is_alpha_code = (c >= iachar('a') .and. c <= iachar('z')) .or. (c >= iachar('A') .and. c <= iachar('Z'))
end function is_alpha_code

elemental logical function is_digit_code(c)
    integer, intent(in) :: c
    is_digit_code = c >= iachar('0') .and. c <= iachar('9')
end function is_digit_code

elemental logical function is_word_code(c)
    !! True for ASCII word characters `[A-Za-z0-9_]`.
    integer, intent(in) :: c
    is_word_code = is_alpha_code(c) .or. is_digit_code(c) .or. c == iachar('_')
end function is_word_code

pure function to_str(i) result(str)
    !! Integer to string, without padding.
    integer, intent(in) :: i
    character(len=:), allocatable :: str
    character(len=16) :: buf
    write (buf, '(i0)') i
    str = trim(buf)
end function to_str

pure subroutine builder_append(self, str)
    class(string_builder_t), intent(inout) :: self
    character(len=*), intent(in) :: str
    character(len=:), allocatable :: tmp

    if (len(str) == 0) return
    if (.not. allocated(self%buf)) allocate (character(len=max(64, len(str))) :: self%buf)
    if (self%n + len(str) > len(self%buf)) then
        allocate (character(len=max(2*len(self%buf), self%n + len(str))) :: tmp)
        tmp(1:self%n) = self%buf(1:self%n)
        call move_alloc(tmp, self%buf)
    end if
    self%buf(self%n + 1:self%n + len(str)) = str
    self%n = self%n + len(str)
end subroutine builder_append

pure subroutine builder_clear(self)
    class(string_builder_t), intent(inout) :: self
    self%n = 0
end subroutine builder_clear

pure function builder_get(self) result(str)
    class(string_builder_t), intent(in) :: self
    character(len=:), allocatable :: str
    if (allocated(self%buf)) then
        str = self%buf(1:self%n)
    else
        str = ''
    end if
end function builder_get

end module fregex_string
