module fregex_match
!! The result of a successful match: the spans of the whole match and of each capture group.
use fregex_string, only: regex_string_t, to_str
implicit none
private

public :: new_match, init_match

type, public :: match_t
    !! Group spans are 1-based and inclusive, so `string(m%start(k):m%end(k))` is the text of
    !! group `k`. An empty match has `end = start - 1`. A group that did not take part in the
    !! match has `start = 0` and `end = -1`, so its text is the empty string.
    private
    character(len=:), allocatable :: text        !! the part of the subject that the groups cover;
                                                 !! allocated only after a successful match
    integer :: offset = 0                        !! position in the subject of `text(1:1)`, minus 1
    integer :: start0 = 0, end0 = -1             !! span of group 0, kept apart from `spans` so that
                                                 !! a pattern without groups needs no array
    integer :: ngroups = 0
    integer, allocatable :: spans(:, :)          !! (2, 1:ngroups), allocated only if ngroups > 0
    type(regex_string_t), allocatable :: names(:)      !! (1:ngroups)
contains
    procedure, private :: group_by_index, group_by_name
    generic :: group => group_by_index, group_by_name
    procedure, private :: start_by_index, start_by_name
    generic :: start => start_by_index, start_by_name
    procedure, private :: end_by_index, end_by_name
    generic :: end => end_by_index, end_by_name
    procedure, private :: matched_by_index, matched_by_name
    generic :: matched => matched_by_index, matched_by_name
    procedure :: group_count
    procedure :: group_index
end type match_t

contains

pure function new_match(subject, caps, names) result(m)
    !! Build a match from the VM's capture slots (start, one-past-end; 0 if unset).
    character(len=*), intent(in) :: subject
    integer, intent(in) :: caps(0:)
    type(regex_string_t), intent(in) :: names(:)
    type(match_t) :: m
    call init_match(m, subject, caps, names)
end function new_match

pure subroutine init_match(m, subject, caps, names)
    !! Like new_match, but fills `m` in place, which avoids copying the result.
    type(match_t), intent(out) :: m
    character(len=*), intent(in) :: subject
    integer, intent(in) :: caps(0:)
    type(regex_string_t), intent(in) :: names(:)
    integer :: k, lo, hi

    ! names are only needed for lookups by name, so they are not copied if no group has one
    do k = 1, size(names)
        if (len(names(k)%s) > 0) then
            m%names = names
            exit
        end if
    end do
    m%start0 = caps(0)
    m%end0 = caps(1) - 1
    lo = m%start0
    hi = m%end0
    m%ngroups = size(caps)/2 - 1
    if (m%ngroups > 0) then
        allocate (m%spans(2, m%ngroups))
        do k = 1, m%ngroups
            if (caps(2*k) > 0 .and. caps(2*k + 1) > 0) then
                m%spans(:, k) = [caps(2*k), caps(2*k + 1) - 1]
                lo = min(lo, m%spans(1, k))
                hi = max(hi, m%spans(2, k))
            else
                m%spans(:, k) = [0, -1]
            end if
        end do
    end if
    ! Only the covered text is kept (groups in a lookaround can lie outside group 0), so that
    ! find_all does not copy the whole subject for every match.
    m%offset = lo - 1
    m%text = subject(lo:hi)
end subroutine init_match

pure integer function group_count(self)
    !! Number of capture groups in the pattern (not counting group 0, the whole match).
    class(match_t), intent(in) :: self
    group_count = self%ngroups
end function group_count

pure integer function group_index(self, name) result(g)
    !! Number of the group called `name`, or 0 if there is none.
    class(match_t), intent(in) :: self
    character(len=*), intent(in) :: name
    if (allocated(self%names)) then
        do g = 1, size(self%names)
            if (len(self%names(g)%s) == len(name)) then
                if (self%names(g)%s == name) return
            end if
        end do
    end if
    g = 0
end function group_index

pure function group_by_index(self, k) result(text)
    !! Text of group `k` (default 0, the whole match).
    class(match_t), intent(in) :: self
    integer, intent(in), optional :: k
    character(len=:), allocatable :: text
    integer :: g
    g = checked_index(self, k)
    text = self%text(span_start(self, g) - self%offset:span_end(self, g) - self%offset)
end function group_by_index

pure function group_by_name(self, name) result(text)
    class(match_t), intent(in) :: self
    character(len=*), intent(in) :: name
    character(len=:), allocatable :: text
    text = self%group_by_index(checked_name(self, name))
end function group_by_name

pure integer function start_by_index(self, k) result(i)
    class(match_t), intent(in) :: self
    integer, intent(in), optional :: k
    i = span_start(self, checked_index(self, k))
end function start_by_index

pure integer function start_by_name(self, name) result(i)
    class(match_t), intent(in) :: self
    character(len=*), intent(in) :: name
    i = span_start(self, checked_name(self, name))
end function start_by_name

pure integer function end_by_index(self, k) result(i)
    class(match_t), intent(in) :: self
    integer, intent(in), optional :: k
    i = span_end(self, checked_index(self, k))
end function end_by_index

pure integer function end_by_name(self, name) result(i)
    class(match_t), intent(in) :: self
    character(len=*), intent(in) :: name
    i = span_end(self, checked_name(self, name))
end function end_by_name

pure logical function matched_by_index(self, k) result(matched)
    !! True if group `k` took part in the match.
    class(match_t), intent(in) :: self
    integer, intent(in), optional :: k
    matched = span_start(self, checked_index(self, k)) > 0
end function matched_by_index

pure logical function matched_by_name(self, name) result(matched)
    class(match_t), intent(in) :: self
    character(len=*), intent(in) :: name
    matched = span_start(self, checked_name(self, name)) > 0
end function matched_by_name

pure integer function checked_index(self, k) result(g)
    class(match_t), intent(in) :: self
    integer, intent(in), optional :: k
    if (.not. allocated(self%text)) error stop 'fregex: match_t used without a successful match'
    g = 0
    if (present(k)) g = k
    if (g < 0 .or. g > self%ngroups) error stop 'fregex: no such group: '//to_str(g)
end function checked_index

pure integer function span_start(self, g) result(i)
    class(match_t), intent(in) :: self
    integer, intent(in) :: g
    if (g == 0) then
        i = self%start0
    else
        i = self%spans(1, g)
    end if
end function span_start

pure integer function span_end(self, g) result(i)
    class(match_t), intent(in) :: self
    integer, intent(in) :: g
    if (g == 0) then
        i = self%end0
    else
        i = self%spans(2, g)
    end if
end function span_end

pure integer function checked_name(self, name) result(g)
    class(match_t), intent(in) :: self
    character(len=*), intent(in) :: name
    g = self%group_index(name)
    if (g == 0) error stop 'fregex: no such group: '//name
end function checked_name

end module fregex_match
