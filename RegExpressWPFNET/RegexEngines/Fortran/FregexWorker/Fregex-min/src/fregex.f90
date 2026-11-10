module fregex
!! Backtracking regular expressions for modern Fortran.
!!
!! The pattern syntax follows Python's `re` module applied to byte strings: ASCII semantics,
!! `\d \w \s \b`, groups (named and non-capturing), alternation, greedy, lazy and possessive
!! quantifiers, lookahead, lookbehind, atomic groups, backreferences and inline flags.
!! Lookbehind may also have a variable width, which Python does not allow.
!!
!! Positions are 1-based and spans are inclusive, so `string(m%start():m%end())` is the match.
!!
!! Procedures that can fail take optional `stat` and `errmsg` arguments. If `stat` is absent
!! and an error occurs, the program stops with an error message.
use iso_fortran_env, only: int64
use fregex_string, only: regex_string_t, string_builder_t
use fregex_parser, only: ast_t, parse_pattern, F_IGNORECASE, F_MULTILINE, F_DOTALL
use fregex_program, only: program_t
use fregex_compiler, only: compile_ast
use fregex_vm, only: vm_t, vm_exec, MODE_SEARCH, MODE_MATCH, MODE_FULLMATCH
use fregex_match, only: match_t, new_match, init_match
use fregex_template, only: template_t, parse_template, expand_template
implicit none
private

public :: regex_t, match_t, regex_string_t
public :: regex
public :: re_search, re_match, re_fullmatch, re_find_all, re_split, re_sub, re_escape
public :: replacement_fn

! flags (combine with ior or +)
integer, parameter, public :: RE_IGNORECASE = F_IGNORECASE   !! case-insensitive matching (ASCII)
integer, parameter, public :: RE_MULTILINE = F_MULTILINE     !! `^` and `$` also match at line breaks
integer, parameter, public :: RE_DOTALL = F_DOTALL           !! `.` also matches a newline

! status codes
integer, parameter, public :: RE_OK = 0
integer, parameter, public :: RE_ERR_SYNTAX = 1        !! invalid pattern
integer, parameter, public :: RE_ERR_STEP_LIMIT = 2    !! matching took more steps than the limit
integer, parameter, public :: RE_ERR_NOT_COMPILED = 3  !! the regex was used before a successful compile
integer, parameter, public :: RE_ERR_TEMPLATE = 4      !! invalid replacement template

integer(int64), parameter, public :: RE_DEFAULT_STEP_LIMIT = 10000000_int64

type :: regex_t
    !! A compiled regular expression.
    private
    type(program_t) :: prog
    character(len=:), allocatable :: pat
    integer :: flags = 0
    integer(int64) :: step_limit = RE_DEFAULT_STEP_LIMIT
    logical :: compiled = .false.
contains
    procedure :: compile => regex_compile
    procedure :: is_compiled => regex_is_compiled
    procedure :: pattern => regex_pattern
    procedure :: group_count => regex_group_count
    procedure :: group_index => regex_group_index
    procedure :: set_step_limit => regex_set_step_limit
    procedure :: search => regex_search
    procedure :: match => regex_match
    procedure :: fullmatch => regex_fullmatch
    procedure :: find_all => regex_find_all
    procedure :: split => regex_split
    procedure, private :: sub_template, sub_callback
    generic :: sub => sub_template, sub_callback
    procedure, private :: exec, exec_one, substitute
end type regex_t

abstract interface
    function replacement_fn(m) result(text)
        !! Computes the replacement text for a match, for `regex_t%sub`.
        import :: match_t
        type(match_t), intent(in) :: m
        character(len=:), allocatable :: text
    end function replacement_fn
end interface

type, abstract :: replacer_t
    !! Produces the replacement text for each match in `substitute`.
contains
    procedure(replace_iface), deferred :: replace
end type replacer_t

abstract interface
    subroutine replace_iface(self, subject, caps, names, out)
        !! Append the replacement for the match in `caps` to `out`. Appending, rather than
        !! returning the text, saves allocating a string for every match.
        import :: replacer_t, regex_string_t, string_builder_t
        class(replacer_t), intent(in) :: self
        character(len=*), intent(in) :: subject
        integer, intent(in) :: caps(0:)
        type(regex_string_t), intent(in) :: names(:)
        type(string_builder_t), intent(inout) :: out
    end subroutine replace_iface
end interface

type, extends(replacer_t) :: template_replacer_t
    type(template_t) :: tmpl
contains
    procedure :: replace => replace_template
end type template_replacer_t

type, extends(replacer_t) :: callback_replacer_t
    procedure(replacement_fn), pointer, nopass :: fn => null()
contains
    procedure :: replace => replace_callback
end type callback_replacer_t

contains

function regex(pattern, flags, stat, errmsg) result(re)
    !! Compile `pattern` into a new regex.
    character(len=*), intent(in) :: pattern
    integer, intent(in), optional :: flags
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    type(regex_t) :: re
    call re%compile(pattern, flags, stat, errmsg)
end function regex

subroutine regex_compile(self, pattern, flags, stat, errmsg)
    class(regex_t), intent(inout) :: self
    character(len=*), intent(in) :: pattern
    integer, intent(in), optional :: flags
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg

    type(ast_t) :: ast
    character(len=:), allocatable :: msg

    self%compiled = .false.
    self%flags = 0
    if (present(flags)) self%flags = flags
    call parse_pattern(pattern, self%flags, ast, msg)
    if (.not. allocated(msg)) call compile_ast(ast, self%prog, msg)
    if (allocated(msg)) then
        call report(RE_ERR_SYNTAX, msg, stat, errmsg)
        return
    end if
    self%pat = pattern
    self%compiled = .true.
    if (present(stat)) stat = RE_OK
end subroutine regex_compile

pure logical function regex_is_compiled(self)
    class(regex_t), intent(in) :: self
    regex_is_compiled = self%compiled
end function regex_is_compiled

pure function regex_pattern(self) result(pattern)
    !! The source pattern.
    class(regex_t), intent(in) :: self
    character(len=:), allocatable :: pattern
    pattern = ''
    if (allocated(self%pat)) pattern = self%pat
end function regex_pattern

pure integer function regex_group_count(self)
    !! Number of capturing groups.
    class(regex_t), intent(in) :: self
    regex_group_count = self%prog%ngroups
end function regex_group_count

pure integer function regex_group_index(self, name) result(g)
    !! Number of the group called `name`, or 0 if there is none.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: name
    do g = 1, self%prog%ngroups
        if (len(self%prog%names(g)%s) == len(name)) then
            if (self%prog%names(g)%s == name) return
        end if
    end do
    g = 0
end function regex_group_index

pure subroutine regex_set_step_limit(self, limit)
    !! Limit the work done by one search (default RE_DEFAULT_STEP_LIMIT), which protects
    !! against patterns with exponential backtracking.
    class(regex_t), intent(inout) :: self
    integer(int64), intent(in) :: limit
    self%step_limit = limit
end subroutine regex_set_step_limit

logical function regex_search(self, string, m, pos, stat, errmsg) result(found)
    !! Find the leftmost match at or after position `pos` (default 1).
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: pos
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    found = self%exec_one(string, MODE_SEARCH, m, pos, stat, errmsg)
end function regex_search

logical function regex_match(self, string, m, pos, stat, errmsg) result(found)
    !! Match only at position `pos` (default 1).
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: pos
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    found = self%exec_one(string, MODE_MATCH, m, pos, stat, errmsg)
end function regex_match

logical function regex_fullmatch(self, string, m, pos, stat, errmsg) result(found)
    !! Match from position `pos` (default 1) to the end of the string.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: pos
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    found = self%exec_one(string, MODE_FULLMATCH, m, pos, stat, errmsg)
end function regex_fullmatch

function regex_find_all(self, string, stat, errmsg) result(matches)
    !! All non-overlapping matches, left to right (like Python's `finditer`).
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    type(match_t), allocatable :: matches(:)

    type(vm_t) :: vm
    integer, allocatable :: caps(:), all_caps(:, :), tmp(:, :)
    integer :: pos, forbid, nfound, ierr, k
    logical :: found
    character(len=:), allocatable :: msg

    nfound = 0
    pos = 1
    forbid = 0
    do
        call self%exec(string, pos, MODE_SEARCH, forbid, vm, found, caps, ierr, msg)
        if (ierr /= RE_OK .or. .not. found) exit
        if (.not. allocated(all_caps)) allocate (all_caps(0:size(caps) - 1, 16))
        if (nfound == size(all_caps, 2)) then
            allocate (tmp(0:size(caps) - 1, 2*nfound))
            tmp(:, 1:nfound) = all_caps(:, 1:nfound)
            call move_alloc(tmp, all_caps)
        end if
        nfound = nfound + 1
        all_caps(:, nfound) = caps
        call advance(caps, pos, forbid)
    end do
    ! The matches are built at the end, so that growing the list only copies integers.
    allocate (matches(nfound))
    do k = 1, nfound
        call init_match(matches(k), string, all_caps(:, k), self%prog%names)
    end do
    call finish(ierr, msg, stat, errmsg)
end function regex_find_all

function regex_split(self, string, maxsplit, stat, errmsg) result(parts)
    !! Split `string` at each match. Text of capture groups is included between the pieces,
    !! with an empty string for a group that did not match. At most `maxsplit` splits are
    !! made if it is present and positive.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    integer, intent(in), optional :: maxsplit
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    type(regex_string_t), allocatable :: parts(:)

    type(vm_t) :: vm
    integer, allocatable :: caps(:), spans(:, :)
    integer :: pos, forbid, last, nsplit, limit, g, ierr, nparts, k
    logical :: found
    character(len=:), allocatable :: msg

    limit = 0
    if (present(maxsplit)) limit = maxsplit
    allocate (spans(2, 16))
    nparts = 0
    last = 1
    nsplit = 0
    pos = 1
    forbid = 0
    ierr = RE_OK
    do
        if (limit > 0 .and. nsplit >= limit) exit
        call self%exec(string, pos, MODE_SEARCH, forbid, vm, found, caps, ierr, msg)
        if (ierr /= RE_OK .or. .not. found) exit
        call add(last, caps(0) - 1)
        do g = 1, self%prog%ngroups
            if (caps(2*g) > 0 .and. caps(2*g + 1) > 0) then
                call add(caps(2*g), caps(2*g + 1) - 1)
            else
                call add(1, 0)
            end if
        end do
        last = caps(1)
        nsplit = nsplit + 1
        call advance(caps, pos, forbid)
    end do
    call add(last, len(string))
    ! The pieces are built at the end, so that growing the list only copies integers.
    allocate (parts(nparts))
    do k = 1, nparts
        parts(k)%s = string(spans(1, k):spans(2, k))
    end do
    call finish(ierr, msg, stat, errmsg)

contains

    subroutine add(i1, i2)
        !! Record the piece `string(i1:i2)`, doubling the list when it is full.
        integer, intent(in) :: i1, i2
        integer, allocatable :: tmp(:, :)
        if (nparts == size(spans, 2)) then
            allocate (tmp(2, 2*nparts))
            tmp(:, 1:nparts) = spans(:, 1:nparts)
            call move_alloc(tmp, spans)
        end if
        nparts = nparts + 1
        spans(:, nparts) = [i1, i2]
    end subroutine add

end function regex_split

function sub_template(self, string, repl, count, nsubs, stat, errmsg) result(out)
    !! Replace matches with the template `repl` (`\1`, `\g<name>`, `\n`, ...). At most `count`
    !! replacements are made if it is present and positive; `nsubs` returns how many were made.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    character(len=*), intent(in) :: repl
    integer, intent(in), optional :: count
    integer, intent(out), optional :: nsubs
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    character(len=:), allocatable :: out

    type(template_t) :: tmpl
    character(len=:), allocatable :: msg

    out = string
    if (present(nsubs)) nsubs = 0
    if (.not. self%compiled) then
        call report(RE_ERR_NOT_COMPILED, 'regex is not compiled', stat, errmsg)
        return
    end if
    call parse_template(repl, self%prog%names, tmpl, msg)
    if (allocated(msg)) then
        call report(RE_ERR_TEMPLATE, msg, stat, errmsg)
        return
    end if
    out = self%substitute(string, template_replacer_t(tmpl), count, nsubs, stat, errmsg)
end function sub_template

function sub_callback(self, string, fn, count, nsubs, stat, errmsg) result(out)
    !! Replace matches with the text returned by `fn` for each match.
    !!
    !! The string comes before `fn`: gfortran 15 miscompiles a dummy procedure with a
    !! deferred-length result that precedes a `character(*)` dummy argument.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    procedure(replacement_fn) :: fn
    integer, intent(in), optional :: count
    integer, intent(out), optional :: nsubs
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    character(len=:), allocatable :: out
    type(callback_replacer_t) :: replacer
    replacer%fn => fn
    out = self%substitute(string, replacer, count, nsubs, stat, errmsg)
end function sub_callback

function substitute(self, string, replacer, count, nsubs, stat, errmsg) result(out)
    !! Shared loop for both kinds of substitution.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    class(replacer_t), intent(in) :: replacer
    integer, intent(in), optional :: count
    integer, intent(out), optional :: nsubs
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    character(len=:), allocatable :: out

    type(string_builder_t) :: buf
    type(vm_t) :: vm
    integer, allocatable :: caps(:)
    integer :: pos, forbid, last, n, limit, ierr
    logical :: found
    character(len=:), allocatable :: msg

    limit = 0
    if (present(count)) limit = count
    last = 1
    n = 0
    pos = 1
    forbid = 0
    ierr = RE_OK
    do
        if (limit > 0 .and. n >= limit) exit
        call self%exec(string, pos, MODE_SEARCH, forbid, vm, found, caps, ierr, msg)
        if (ierr /= RE_OK .or. .not. found) exit
        call buf%append(string(last:caps(0) - 1))
        call replacer%replace(string, caps, self%prog%names, buf)
        last = caps(1)
        n = n + 1
        call advance(caps, pos, forbid)
    end do
    call buf%append(string(last:))
    out = buf%get()
    if (present(nsubs)) nsubs = n
    if (ierr /= RE_OK) out = string
    call finish(ierr, msg, stat, errmsg)
end function substitute

subroutine replace_template(self, subject, caps, names, out)
    class(template_replacer_t), intent(in) :: self
    character(len=*), intent(in) :: subject
    integer, intent(in) :: caps(0:)
    type(regex_string_t), intent(in) :: names(:)
    type(string_builder_t), intent(inout) :: out
    call expand_template(self%tmpl, subject, caps, out)
end subroutine replace_template

subroutine replace_callback(self, subject, caps, names, out)
    class(callback_replacer_t), intent(in) :: self
    character(len=*), intent(in) :: subject
    integer, intent(in) :: caps(0:)
    type(regex_string_t), intent(in) :: names(:)
    type(string_builder_t), intent(inout) :: out
    call out%append(self%fn(new_match(subject, caps, names)))
end subroutine replace_callback

logical function exec_one(self, string, mode, m, pos, stat, errmsg) result(found)
    !! One search, match or fullmatch, with error reporting.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    integer, intent(in) :: mode
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: pos
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg

    type(vm_t) :: vm
    integer, allocatable :: caps(:)
    integer :: start, ierr
    character(len=:), allocatable :: msg

    start = 1
    if (present(pos)) start = max(1, pos)
    call self%exec(string, start, mode, 0, vm, found, caps, ierr, msg)
    if (found .and. present(m)) call init_match(m, string, caps, self%prog%names)
    call finish(ierr, msg, stat, errmsg)
end function exec_one

subroutine exec(self, string, start, mode, forbid, vm, found, caps, ierr, msg)
    !! Run the VM. `ierr` is RE_OK or an error code, with `msg` describing it. `vm` is work
    !! space that callers making several searches reuse.
    class(regex_t), intent(in) :: self
    character(len=*), intent(in) :: string
    integer, intent(in) :: start, mode, forbid
    type(vm_t), intent(inout) :: vm
    logical, intent(out) :: found
    integer, allocatable, intent(inout) :: caps(:)
    integer, intent(out) :: ierr
    character(len=:), allocatable, intent(inout) :: msg
    logical :: overflow

    found = .false.
    ierr = RE_OK
    if (.not. self%compiled) then
        ierr = RE_ERR_NOT_COMPILED
        msg = 'regex is not compiled'
        return
    end if
    if (start > len(string) + 1) return
    call vm_exec(self%prog, string, start, mode, forbid, self%step_limit, vm, found, caps, overflow)
    if (overflow) then
        found = .false.
        ierr = RE_ERR_STEP_LIMIT
        msg = 'step limit exceeded while matching pattern: '//self%pat
    end if
end subroutine exec

pure subroutine advance(caps, pos, forbid)
    !! Where to search next after a match. After an empty match the next search starts at the
    !! same position but may not produce another empty match there (Python 3.7+ semantics).
    integer, intent(in) :: caps(0:)
    integer, intent(out) :: pos, forbid
    pos = caps(1)
    forbid = merge(caps(1), 0, caps(0) == caps(1))
end subroutine advance

subroutine finish(ierr, msg, stat, errmsg)
    integer, intent(in) :: ierr
    character(len=:), allocatable, intent(in) :: msg
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    if (ierr /= RE_OK) then
        call report(ierr, msg, stat, errmsg)
    else if (present(stat)) then
        stat = RE_OK
    end if
end subroutine finish

subroutine report(ierr, msg, stat, errmsg)
    integer, intent(in) :: ierr
    character(len=*), intent(in) :: msg
    integer, intent(out), optional :: stat
    character(len=:), allocatable, intent(out), optional :: errmsg
    if (.not. present(stat)) error stop 'fregex: '//msg
    stat = ierr
    if (present(errmsg)) errmsg = msg
end subroutine report

!--------------------------------------------------------------------------------------------
! Convenience procedures that compile the pattern on each call. Invalid patterns stop the
! program; compile a regex_t to handle errors or to reuse a pattern.
!--------------------------------------------------------------------------------------------

logical function re_search(pattern, string, m, flags) result(found)
    character(len=*), intent(in) :: pattern, string
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: flags
    type(regex_t) :: re
    call re%compile(pattern, flags)
    found = re%search(string, m)
end function re_search

logical function re_match(pattern, string, m, flags) result(found)
    character(len=*), intent(in) :: pattern, string
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: flags
    type(regex_t) :: re
    call re%compile(pattern, flags)
    found = re%match(string, m)
end function re_match

logical function re_fullmatch(pattern, string, m, flags) result(found)
    character(len=*), intent(in) :: pattern, string
    type(match_t), intent(out), optional :: m
    integer, intent(in), optional :: flags
    type(regex_t) :: re
    call re%compile(pattern, flags)
    found = re%fullmatch(string, m)
end function re_fullmatch

function re_find_all(pattern, string, flags) result(matches)
    character(len=*), intent(in) :: pattern, string
    integer, intent(in), optional :: flags
    type(match_t), allocatable :: matches(:)
    type(regex_t) :: re
    call re%compile(pattern, flags)
    matches = re%find_all(string)
end function re_find_all

function re_split(pattern, string, maxsplit, flags) result(parts)
    character(len=*), intent(in) :: pattern, string
    integer, intent(in), optional :: maxsplit, flags
    type(regex_string_t), allocatable :: parts(:)
    type(regex_t) :: re
    call re%compile(pattern, flags)
    parts = re%split(string, maxsplit)
end function re_split

function re_sub(pattern, string, repl, count, flags) result(out)
    character(len=*), intent(in) :: pattern, string, repl
    integer, intent(in), optional :: count, flags
    character(len=:), allocatable :: out
    type(regex_t) :: re
    call re%compile(pattern, flags)
    out = re%sub(string, repl, count)
end function re_sub

pure function re_escape(string) result(escaped)
    !! Escape the characters that have a special meaning in patterns.
    character(len=*), intent(in) :: string
    character(len=:), allocatable :: escaped
    character(len=*), parameter :: special = '()[]{}?*+-|^$\.&~# '//achar(9)//achar(10)//achar(11)//achar(12)//achar(13)
    type(string_builder_t) :: buf
    integer :: i
    do i = 1, len(string)
        if (index(special, string(i:i)) > 0) call buf%append('\')
        call buf%append(string(i:i))
    end do
    escaped = buf%get()
end function re_escape

end module fregex
