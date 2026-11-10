module fregex_parser
!! Regular expression parser: turns a pattern string into an abstract syntax tree (AST).
!!
!! The syntax follows Python's `re` module for byte strings (ASCII semantics). Backslashes are
!! literal in Fortran strings, so patterns are written exactly like Python raw strings.
use fregex_charset, only: charset_t, digit_set, word_set, space_set
use fregex_string, only: regex_string_t, to_str, is_alpha_code, is_digit_code, is_word_code
implicit none
private

public :: parse_pattern

! node kinds
integer, parameter, public :: N_EMPTY = 0    !! matches the empty string
integer, parameter, public :: N_CHAR = 1     !! a = byte, b = 1 if case-insensitive
integer, parameter, public :: N_ANY = 2      !! a = 1 if it also matches a newline
integer, parameter, public :: N_SET = 3      !! a = index into ast%sets
integer, parameter, public :: N_ASSERT = 4   !! a = assertion kind (A_*)
integer, parameter, public :: N_CONCAT = 5   !! kids matched in sequence
integer, parameter, public :: N_ALT = 6      !! kids tried in order
integer, parameter, public :: N_GROUP = 7    !! a = group number, kids(1) = body
integer, parameter, public :: N_REPEAT = 8   !! a = min, b = max (or INF), c = R_* mode, kids(1) = body
integer, parameter, public :: N_LOOK = 9     !! a = 1 lookahead / 0 lookbehind, b = 1 if negated, kids(1) = body
integer, parameter, public :: N_ATOMIC = 10  !! kids(1) = body
integer, parameter, public :: N_BACKREF = 11 !! a = group number, b = 1 if case-insensitive

! assertion kinds
integer, parameter, public :: A_BOL = 1      !! `^`
integer, parameter, public :: A_BOL_M = 2    !! `^` in multiline mode
integer, parameter, public :: A_EOL = 3      !! `$`
integer, parameter, public :: A_EOL_M = 4    !! `$` in multiline mode
integer, parameter, public :: A_BOS = 5      !! `\A`
integer, parameter, public :: A_EOS = 6      !! `\Z`
integer, parameter, public :: A_WORDB = 7    !! `\b`
integer, parameter, public :: A_NWORDB = 8   !! `\B`

! repeat modes
integer, parameter, public :: R_GREEDY = 0, R_LAZY = 1, R_POSSESSIVE = 2

integer, parameter, public :: INF = -1       !! unbounded repeat count or width

! flags
integer, parameter, public :: F_IGNORECASE = 1
integer, parameter, public :: F_MULTILINE = 2
integer, parameter, public :: F_DOTALL = 4

integer, parameter :: MAX_REPEAT = 100000

type, public :: node_t
    integer :: kind = N_EMPTY
    integer :: a = 0, b = 0, c = 0
    integer, allocatable :: kids(:)
end type node_t

type, public :: ast_t
    type(node_t), allocatable :: nodes(:)
    integer :: n = 0
    integer :: root = 0
    type(charset_t), allocatable :: sets(:)
    integer :: nsets = 0
    integer :: ngroups = 0
    type(regex_string_t), allocatable :: names(:)  !! (1:ngroups), empty for unnamed groups
    logical, allocatable :: closed(:)        !! (1:ngroups), true once the group's `)` is parsed
end type ast_t

type :: parser_t
    character(len=:), allocatable :: p
    integer :: i = 1
    type(ast_t) :: ast
    character(len=:), allocatable :: errmsg
end type parser_t

contains

subroutine parse_pattern(pattern, flags, ast, errmsg)
    !! Parse `pattern` into `ast`. On a syntax error, `errmsg` is allocated with a description.
    character(len=*), intent(in) :: pattern
    integer, intent(in) :: flags
    type(ast_t), intent(out) :: ast
    character(len=:), allocatable, intent(out) :: errmsg

    type(parser_t) :: ps
    integer :: f

    ps%p = pattern
    allocate (ps%ast%nodes(16), ps%ast%sets(4), ps%ast%names(0), ps%ast%closed(0))
    f = flags
    ps%ast%root = parse_alt(ps, f)
    ! only an unmatched ')' can stop the top-level alternation early
    if (.not. failed(ps) .and. .not. at_end(ps)) call set_error(ps, 'unbalanced parenthesis', ps%i)
    if (failed(ps)) then
        errmsg = ps%errmsg
        return
    end if
    ast = ps%ast
end subroutine parse_pattern

recursive function parse_alt(ps, flags) result(id)
    !! alternation := concat ('|' concat)*
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer :: id
    integer, allocatable :: kids(:)
    integer :: k

    id = 0
    k = parse_concat(ps, flags)
    if (failed(ps)) return
    kids = [k]
    do
        if (at_end(ps)) exit
        if (peek(ps) /= '|') exit
        ps%i = ps%i + 1
        k = parse_concat(ps, flags)
        if (failed(ps)) return
        kids = [kids, k]
    end do
    if (size(kids) == 1) then
        id = kids(1)
    else
        id = new_node(ps, N_ALT)
        ps%ast%nodes(id)%kids = kids
    end if
end function parse_alt

recursive function parse_concat(ps, flags) result(id)
    !! concat := quantified*
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer :: id
    integer, allocatable :: kids(:)
    integer :: k

    id = 0
    allocate (kids(0))
    do
        if (at_end(ps)) exit
        if (peek(ps) == '|' .or. peek(ps) == ')') exit
        k = parse_quantified(ps, flags)
        if (failed(ps)) return
        if (k > 0) kids = [kids, k]
    end do
    select case (size(kids))
    case (0)
        id = new_node(ps, N_EMPTY)
    case (1)
        id = kids(1)
    case default
        id = new_node(ps, N_CONCAT)
        ps%ast%nodes(id)%kids = kids
    end select
end function parse_concat

recursive function parse_quantified(ps, flags) result(id)
    !! quantified := atom quantifier?
    !!
    !! Returns 0 when the atom produced no node (an inline flag group or a comment).
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer :: id
    integer :: body, qmin, qmax, mode, qstart
    logical :: bare_assert

    bare_assert = is_bare_assertion(ps)
    body = parse_atom(ps, flags)
    id = body
    if (failed(ps) .or. at_end(ps)) return

    qstart = ps%i
    if (.not. parse_quantifier(ps, qmin, qmax)) return
    if (failed(ps)) return
    if (body == 0 .or. bare_assert) then
        call set_error(ps, 'nothing to repeat', qstart)
        return
    end if

    mode = R_GREEDY
    if (.not. at_end(ps)) then
        if (peek(ps) == '?') then
            mode = R_LAZY
            ps%i = ps%i + 1
        else if (peek(ps) == '+') then
            mode = R_POSSESSIVE
            ps%i = ps%i + 1
        end if
    end if
    id = new_node(ps, N_REPEAT, qmin, qmax, mode)
    ps%ast%nodes(id)%kids = [body]

    if (looks_like_quantifier(ps)) call set_error(ps, 'multiple repeat', ps%i)
end function parse_quantified

pure logical function is_bare_assertion(ps)
    !! True if the next atom is an assertion that cannot be repeated (`^`, `$`, `\b`, ...).
    type(parser_t), intent(in) :: ps
    is_bare_assertion = .false.
    if (at_end(ps)) return
    select case (peek(ps))
    case ('^', '$')
        is_bare_assertion = .true.
    case ('\')
        select case (peek(ps, 1))
        case ('b', 'B', 'A', 'Z')
            is_bare_assertion = .true.
        end select
    end select
end function is_bare_assertion

logical function parse_quantifier(ps, qmin, qmax) result(found)
    !! Parse `*`, `+`, `?` or `{m,n}` at the current position, advancing past it.
    !! A `{` that does not form a valid quantifier is not consumed (it is then a literal).
    type(parser_t), intent(inout) :: ps
    integer, intent(out) :: qmin, qmax

    found = .true.
    select case (peek(ps))
    case ('*')
        qmin = 0
        qmax = INF
    case ('+')
        qmin = 1
        qmax = INF
    case ('?')
        qmin = 0
        qmax = 1
    case ('{')
        found = parse_braces(ps, qmin, qmax)
        return
    case default
        found = .false.
        return
    end select
    ps%i = ps%i + 1
end function parse_quantifier

logical function parse_braces(ps, qmin, qmax) result(found)
    !! `{n}`, `{n,}`, `{,m}`, `{n,m}` or `{,}`.
    type(parser_t), intent(inout) :: ps
    integer, intent(out) :: qmin, qmax
    integer :: j, lo_start, lo_end, hi_start, hi_end
    logical :: comma

    found = .false.
    qmin = 0
    qmax = INF
    j = ps%i + 1
    lo_start = j
    do while (j <= len(ps%p))
        if (.not. is_digit_code(iachar(ps%p(j:j)))) exit
        j = j + 1
    end do
    lo_end = j - 1
    comma = .false.
    hi_start = j
    hi_end = j - 1
    if (j <= len(ps%p)) then
        if (ps%p(j:j) == ',') then
            comma = .true.
            j = j + 1
            hi_start = j
            do while (j <= len(ps%p))
                if (.not. is_digit_code(iachar(ps%p(j:j)))) exit
                j = j + 1
            end do
            hi_end = j - 1
        end if
    end if
    if (j > len(ps%p)) return
    if (ps%p(j:j) /= '}') return
    if (lo_end < lo_start .and. .not. comma) return   ! "{}" is a literal

    found = .true.
    if (lo_end >= lo_start) qmin = read_count(ps, ps%p(lo_start:lo_end), lo_start)
    if (comma) then
        if (hi_end >= hi_start) qmax = read_count(ps, ps%p(hi_start:hi_end), hi_start)
    else
        qmax = qmin
    end if
    if (failed(ps)) return
    if (qmax /= INF .and. qmax < qmin) then
        call set_error(ps, 'min repeat greater than max repeat', hi_start)
        return
    end if
    ps%i = j + 1
end function parse_braces

integer function read_count(ps, digits, pos) result(count)
    type(parser_t), intent(inout) :: ps
    character(len=*), intent(in) :: digits
    integer, intent(in) :: pos
    if (len(digits) > 6) then
        count = MAX_REPEAT + 1
    else
        read (digits, *) count
    end if
    if (count > MAX_REPEAT) call set_error(ps, 'the repetition number is too large', pos)
end function read_count

logical function looks_like_quantifier(ps)
    !! True if a quantifier starts at the current position (without consuming it).
    type(parser_t), intent(inout) :: ps
    integer :: save_i, qmin, qmax
    character(len=:), allocatable :: save_err

    looks_like_quantifier = .false.
    if (at_end(ps)) return
    select case (peek(ps))
    case ('*', '+', '?')
        looks_like_quantifier = .true.
    case ('{')
        save_i = ps%i
        if (allocated(ps%errmsg)) save_err = ps%errmsg
        looks_like_quantifier = parse_braces(ps, qmin, qmax)
        ps%i = save_i
        if (allocated(save_err)) then
            ps%errmsg = save_err
        else if (allocated(ps%errmsg)) then
            deallocate (ps%errmsg)
        end if
    end select
end function looks_like_quantifier

recursive function parse_atom(ps, flags) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer :: id

    id = 0
    select case (peek(ps))
    case ('(')
        id = parse_group(ps, flags)
    case ('[')
        id = parse_class(ps, flags)
    case ('.')
        ps%i = ps%i + 1
        id = new_node(ps, N_ANY, merge(1, 0, iand(flags, F_DOTALL) /= 0))
    case ('^')
        ps%i = ps%i + 1
        id = new_node(ps, N_ASSERT, merge(A_BOL_M, A_BOL, iand(flags, F_MULTILINE) /= 0))
    case ('$')
        ps%i = ps%i + 1
        id = new_node(ps, N_ASSERT, merge(A_EOL_M, A_EOL, iand(flags, F_MULTILINE) /= 0))
    case ('\')
        id = parse_escape(ps, flags)
    case ('*', '+', '?')
        call set_error(ps, 'nothing to repeat', ps%i)
    case ('{')
        if (looks_like_quantifier(ps)) then
            call set_error(ps, 'nothing to repeat', ps%i)
        else
            ps%i = ps%i + 1
            id = char_node(ps, iachar('{'), flags)
        end if
    case default
        id = char_node(ps, iachar(ps%p(ps%i:ps%i)), flags)
        ps%i = ps%i + 1
    end select
end function parse_atom

function parse_escape(ps, flags) result(id)
    !! A backslash escape outside a character class.
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: flags
    integer :: id
    integer :: start, code, g
    character :: c
    type(charset_t) :: set

    id = 0
    start = ps%i
    ps%i = ps%i + 1
    if (at_end(ps)) then
        call set_error(ps, 'bad escape (end of pattern)', start)
        return
    end if
    c = peek(ps)
    ps%i = ps%i + 1
    select case (c)
    case ('d', 'D', 'w', 'W', 's', 'S')
        set = class_escape_set(c)
        id = set_node(ps, set)
    case ('b')
        id = new_node(ps, N_ASSERT, A_WORDB)
    case ('B')
        id = new_node(ps, N_ASSERT, A_NWORDB)
    case ('A')
        id = new_node(ps, N_ASSERT, A_BOS)
    case ('Z')
        id = new_node(ps, N_ASSERT, A_EOS)
    case ('0')
        code = read_octal(ps, 0, 2)
        id = char_node(ps, code, flags)
    case ('1':'9')
        if (is_octal(peek(ps)) .and. is_octal(peek(ps, 1)) .and. is_octal(c)) then
            code = read_octal(ps, iachar(c) - iachar('0'), 2)
            if (code > 255) then
                call set_error(ps, 'octal escape value outside of range 0-0o377', start)
                return
            end if
            id = char_node(ps, code, flags)
            return
        end if
        g = iachar(c) - iachar('0')
        if (.not. at_end(ps)) then
            if (is_digit_code(iachar(peek(ps)))) then
                g = 10*g + iachar(peek(ps)) - iachar('0')
                ps%i = ps%i + 1
            end if
        end if
        id = backref_node(ps, g, flags, start)
    case default
        code = simple_escape(ps, c, start, in_class=.false.)
        if (failed(ps)) return
        id = char_node(ps, code, flags)
    end select
end function parse_escape

integer function simple_escape(ps, c, start, in_class) result(code)
    !! Escapes that stand for one byte: `\n`, `\t`, `\xhh`, `\.` etc. Unknown letters are errors.
    type(parser_t), intent(inout) :: ps
    character, intent(in) :: c
    integer, intent(in) :: start
    logical, intent(in) :: in_class
    integer :: j, h

    code = iachar(c)
    select case (c)
    case ('n')
        code = 10
    case ('t')
        code = 9
    case ('r')
        code = 13
    case ('f')
        code = 12
    case ('v')
        code = 11
    case ('a')
        code = 7
    case ('b')
        if (in_class) then
            code = 8
        else
            call set_error(ps, 'bad escape \b', start)
        end if
    case ('x')
        code = 0
        do j = 1, 2
            h = hex_value(peek(ps))
            if (at_end(ps) .or. h < 0) then
                call set_error(ps, 'incomplete escape \x', start)
                return
            end if
            code = 16*code + h
            ps%i = ps%i + 1
        end do
    case default
        if (is_alpha_code(iachar(c))) call set_error(ps, 'bad escape \'//c, start)
    end select
end function simple_escape

function parse_class(ps, flags) result(id)
    !! `[...]` character class.
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: flags
    integer :: id
    type(charset_t) :: set, item_set, hi_set
    integer :: start, lo, hi, range_start
    logical :: negate, first, lo_is_set, hi_is_set

    id = 0
    start = ps%i
    ps%i = ps%i + 1
    negate = .false.
    if (peek(ps) == '^' .and. .not. at_end(ps)) then
        negate = .true.
        ps%i = ps%i + 1
    end if
    first = .true.
    do
        if (at_end(ps)) then
            call set_error(ps, 'unterminated character set', start)
            return
        end if
        if (peek(ps) == ']' .and. .not. first) then
            ps%i = ps%i + 1
            exit
        end if
        first = .false.
        range_start = ps%i
        call class_item(ps, lo, lo_is_set, item_set)
        if (failed(ps)) return
        if (peek(ps) == '-' .and. ps%i + 1 <= len(ps%p) .and. peek(ps, 1) /= ']') then
            if (lo_is_set) then
                call set_error(ps, 'bad character range', range_start)
                return
            end if
            ps%i = ps%i + 1
            call class_item(ps, hi, hi_is_set, hi_set)
            if (failed(ps)) return
            if (hi_is_set .or. hi < lo) then
                call set_error(ps, 'bad character range', range_start)
                return
            end if
            call set%add_range(lo, hi)
        else if (lo_is_set) then
            call set%add_set(item_set)
        else
            call set%add(lo)
        end if
    end do
    if (iand(flags, F_IGNORECASE) /= 0) call set%fold_case()
    if (negate) call set%negate()
    id = set_node(ps, set)
end function parse_class

subroutine class_item(ps, code, is_set, set)
    !! One member of a character class: a byte (`code`) or a predefined class (`set`).
    type(parser_t), intent(inout) :: ps
    integer, intent(out) :: code
    logical, intent(out) :: is_set
    type(charset_t), intent(out) :: set
    integer :: start
    character :: c

    is_set = .false.
    code = iachar(peek(ps))
    if (peek(ps) /= '\') then
        ps%i = ps%i + 1
        return
    end if
    start = ps%i
    ps%i = ps%i + 1
    if (at_end(ps)) then
        call set_error(ps, 'unterminated character set', start)
        return
    end if
    c = peek(ps)
    ps%i = ps%i + 1
    select case (c)
    case ('d', 'D', 'w', 'W', 's', 'S')
        is_set = .true.
        set = class_escape_set(c)
    case ('0':'7')
        code = read_octal(ps, iachar(c) - iachar('0'), 2)
        if (code > 255) call set_error(ps, 'octal escape value outside of range 0-0o377', start)
    case ('8', '9')
        call set_error(ps, 'bad escape \'//c, start)
    case default
        code = simple_escape(ps, c, start, in_class=.true.)
    end select
end subroutine class_item

pure function class_escape_set(c) result(set)
    character, intent(in) :: c
    type(charset_t) :: set
    select case (c)
    case ('d', 'D')
        set = digit_set()
    case ('w', 'W')
        set = word_set()
    case ('s', 'S')
        set = space_set()
    end select
    if (c == 'D' .or. c == 'W' .or. c == 'S') call set%negate()
end function class_escape_set

recursive function parse_group(ps, flags) result(id)
    !! Groups and extensions: `(...)`, `(?:...)`, `(?P<name>...)`, lookarounds, `(?>...)`, flags, comments.
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer :: id
    integer :: start, local, inner
    logical :: negated

    id = 0
    start = ps%i
    ps%i = ps%i + 1
    if (peek(ps) /= '?' .or. at_end(ps)) then
        id = parse_capture(ps, flags, start, '')
        return
    end if
    ps%i = ps%i + 1
    if (at_end(ps)) then
        call set_error(ps, 'unexpected end of pattern', ps%i)
        return
    end if
    select case (peek(ps))
    case (':')
        ps%i = ps%i + 1
        local = flags
        id = parse_body(ps, local, start)
    case ('P')
        ps%i = ps%i + 1
        if (peek(ps) == '<') then
            ps%i = ps%i + 1
            id = parse_named_group(ps, flags, start)
        else if (peek(ps) == '=') then
            ps%i = ps%i + 1
            id = parse_named_backref(ps, flags, start)
        else
            call set_error(ps, 'unknown extension ?P'//peek(ps), start + 1)
        end if
    case ('<')
        ps%i = ps%i + 1
        if (peek(ps) == '=' .or. peek(ps) == '!') then
            negated = peek(ps) == '!'
            ps%i = ps%i + 1
            local = flags
            inner = parse_body(ps, local, start)
            if (failed(ps)) return
            id = new_node(ps, N_LOOK, 0, merge(1, 0, negated))
            ps%ast%nodes(id)%kids = [inner]
        else
            id = parse_named_group(ps, flags, start)
        end if
    case ('=', '!')
        negated = peek(ps) == '!'
        ps%i = ps%i + 1
        local = flags
        inner = parse_body(ps, local, start)
        if (failed(ps)) return
        id = new_node(ps, N_LOOK, 1, merge(1, 0, negated))
        ps%ast%nodes(id)%kids = [inner]
    case ('>')
        ps%i = ps%i + 1
        local = flags
        inner = parse_body(ps, local, start)
        if (failed(ps)) return
        id = new_node(ps, N_ATOMIC)
        ps%ast%nodes(id)%kids = [inner]
    case ('#')
        do while (.not. at_end(ps))
            if (peek(ps) == ')') exit
            ps%i = ps%i + 1
        end do
        if (at_end(ps)) then
            call set_error(ps, 'missing ), unterminated comment', start)
            return
        end if
        ps%i = ps%i + 1
    case default
        id = parse_flag_group(ps, flags, start)
    end select
end function parse_group

recursive function parse_body(ps, flags, start) result(inner)
    !! The body of a group up to and including its closing `)`.
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer, intent(in) :: start
    integer :: inner

    inner = parse_alt(ps, flags)
    if (failed(ps)) return
    if (at_end(ps)) then
        call set_error(ps, 'missing ), unterminated subpattern', start)
    else if (peek(ps) /= ')') then
        call set_error(ps, 'missing ), unterminated subpattern', start)
    else
        ps%i = ps%i + 1
    end if
end function parse_body

recursive function parse_capture(ps, flags, start, name) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: flags
    integer, intent(in) :: start
    character(len=*), intent(in) :: name
    integer :: id
    integer :: g, local, inner

    id = 0
    ps%ast%ngroups = ps%ast%ngroups + 1
    g = ps%ast%ngroups
    ps%ast%names = [ps%ast%names, regex_string_t(name)]
    ps%ast%closed = [ps%ast%closed, .false.]
    local = flags
    inner = parse_body(ps, local, start)
    if (failed(ps)) return
    ps%ast%closed(g) = .true.
    id = new_node(ps, N_GROUP, g)
    ps%ast%nodes(id)%kids = [inner]
end function parse_capture

recursive function parse_named_group(ps, flags, start) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: flags
    integer, intent(in) :: start
    integer :: id
    character(len=:), allocatable :: name

    id = 0
    name = read_name(ps, '>')
    if (failed(ps)) return
    if (find_group(ps%ast, name) > 0) then
        call set_error(ps, "redefinition of group name '"//name//"'", start)
        return
    end if
    id = parse_capture(ps, flags, start, name)
end function parse_named_group

function parse_named_backref(ps, flags, start) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: flags
    integer, intent(in) :: start
    integer :: id
    character(len=:), allocatable :: name
    integer :: g

    id = 0
    name = read_name(ps, ')')
    if (failed(ps)) return
    g = find_group(ps%ast, name)
    if (g == 0) then
        call set_error(ps, "unknown group name '"//name//"'", start)
        return
    end if
    id = backref_node(ps, g, flags, start)
end function parse_named_backref

recursive function parse_flag_group(ps, flags, start) result(id)
    !! Inline flags `(?im)` (apply to the rest of the enclosing group) or scoped `(?i-m:...)`.
    type(parser_t), intent(inout) :: ps
    integer, intent(inout) :: flags
    integer, intent(in) :: start
    integer :: id
    integer :: on, off, bit, local
    logical :: in_off

    id = 0
    on = 0
    off = 0
    in_off = .false.
    do
        if (at_end(ps)) then
            call set_error(ps, 'missing -, : or )', ps%i)
            return
        end if
        bit = 0
        select case (peek(ps))
        case ('i')
            bit = F_IGNORECASE
        case ('m')
            bit = F_MULTILINE
        case ('s')
            bit = F_DOTALL
        case ('a', 'u', 'L')
            ! ASCII semantics are the only ones supported; accept and ignore the flags
            if (in_off) then
                call set_error(ps, 'bad inline flags: cannot turn off flags ''a'', ''u'' and ''L''', ps%i)
                return
            end if
        case ('-')
            if (in_off) then
                call set_error(ps, 'bad inline flags: flag turned on and off', ps%i)
                return
            end if
            in_off = .true.
        case (':')
            ps%i = ps%i + 1
            local = iand(ior(flags, on), not(off))
            id = parse_body(ps, local, start)
            return
        case (')')
            if (in_off) then
                call set_error(ps, 'missing :', ps%i)
                return
            end if
            ps%i = ps%i + 1
            flags = ior(flags, on)
            return
        case default
            if (is_alpha_code(iachar(peek(ps)))) then
                call set_error(ps, 'unknown flag', ps%i)
            else
                call set_error(ps, 'unknown extension ?'//peek(ps), start + 1)
            end if
            return
        end select
        if (in_off) then
            off = ior(off, bit)
        else
            on = ior(on, bit)
        end if
        ps%i = ps%i + 1
    end do
end function parse_flag_group

function read_name(ps, terminator) result(name)
    !! Read a group name up to `terminator` and check that it is an identifier.
    type(parser_t), intent(inout) :: ps
    character, intent(in) :: terminator
    character(len=:), allocatable :: name
    integer :: j, k

    name = ''
    j = index(ps%p(ps%i:), terminator)
    if (j == 0) then
        call set_error(ps, 'missing '//terminator//', unterminated name', ps%i)
        return
    end if
    name = ps%p(ps%i:ps%i + j - 2)
    if (len(name) == 0) then
        call set_error(ps, 'missing group name', ps%i)
        return
    end if
    do k = 1, len(name)
        if (.not. is_word_code(iachar(name(k:k))) .or. (k == 1 .and. is_digit_code(iachar(name(1:1))))) then
            call set_error(ps, "bad character in group name '"//name//"'", ps%i)
            return
        end if
    end do
    ps%i = ps%i + j
end function read_name

pure integer function find_group(ast, name) result(g)
    type(ast_t), intent(in) :: ast
    character(len=*), intent(in) :: name
    do g = 1, ast%ngroups
        if (ast%names(g)%s == name .and. len(ast%names(g)%s) == len(name)) return
    end do
    g = 0
end function find_group

function backref_node(ps, g, flags, start) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: g, flags, start
    integer :: id

    id = 0
    if (g > ps%ast%ngroups) then
        call set_error(ps, 'invalid group reference '//to_str(g), start)
    else if (.not. ps%ast%closed(g)) then
        call set_error(ps, 'cannot refer to an open group', start)
    else
        id = new_node(ps, N_BACKREF, g, merge(1, 0, iand(flags, F_IGNORECASE) /= 0))
    end if
end function backref_node

function char_node(ps, code, flags) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: code, flags
    integer :: id
    logical :: icase
    icase = iand(flags, F_IGNORECASE) /= 0 .and. is_alpha_code(code)
    id = new_node(ps, N_CHAR, code, merge(1, 0, icase))
end function char_node

function set_node(ps, set) result(id)
    type(parser_t), intent(inout) :: ps
    type(charset_t), intent(in) :: set
    integer :: id
    type(charset_t), allocatable :: tmp(:)

    if (ps%ast%nsets == size(ps%ast%sets)) then
        allocate (tmp(2*size(ps%ast%sets)))
        tmp(1:ps%ast%nsets) = ps%ast%sets(1:ps%ast%nsets)
        call move_alloc(tmp, ps%ast%sets)
    end if
    ps%ast%nsets = ps%ast%nsets + 1
    ps%ast%sets(ps%ast%nsets) = set
    id = new_node(ps, N_SET, ps%ast%nsets)
end function set_node

function new_node(ps, kind, a, b, c) result(id)
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: kind
    integer, intent(in), optional :: a, b, c
    integer :: id
    type(node_t), allocatable :: tmp(:)

    if (ps%ast%n == size(ps%ast%nodes)) then
        allocate (tmp(2*size(ps%ast%nodes)))
        tmp(1:ps%ast%n) = ps%ast%nodes(1:ps%ast%n)
        call move_alloc(tmp, ps%ast%nodes)
    end if
    ps%ast%n = ps%ast%n + 1
    id = ps%ast%n
    ps%ast%nodes(id)%kind = kind
    if (present(a)) ps%ast%nodes(id)%a = a
    if (present(b)) ps%ast%nodes(id)%b = b
    if (present(c)) ps%ast%nodes(id)%c = c
end function new_node

integer function read_octal(ps, code, max_digits) result(val)
    !! Read up to `max_digits` more octal digits, starting from the value `code`.
    type(parser_t), intent(inout) :: ps
    integer, intent(in) :: code, max_digits
    integer :: k
    val = code
    do k = 1, max_digits
        if (at_end(ps)) exit
        if (.not. is_octal(peek(ps))) exit
        val = 8*val + iachar(peek(ps)) - iachar('0')
        ps%i = ps%i + 1
    end do
end function read_octal

elemental logical function is_octal(c)
    character, intent(in) :: c
    is_octal = c >= '0' .and. c <= '7'
end function is_octal

elemental integer function hex_value(c) result(h)
    character, intent(in) :: c
    select case (c)
    case ('0':'9')
        h = iachar(c) - iachar('0')
    case ('a':'f')
        h = iachar(c) - iachar('a') + 10
    case ('A':'F')
        h = iachar(c) - iachar('A') + 10
    case default
        h = -1
    end select
end function hex_value

pure character function peek(ps, offset)
    !! Character at the current position plus `offset`, or `achar(0)` past the end.
    !! Callers that must tell a real NUL from the end use [[at_end]].
    type(parser_t), intent(in) :: ps
    integer, intent(in), optional :: offset
    integer :: j
    j = ps%i
    if (present(offset)) j = j + offset
    if (j > len(ps%p)) then
        peek = achar(0)
    else
        peek = ps%p(j:j)
    end if
end function peek

pure logical function at_end(ps)
    type(parser_t), intent(in) :: ps
    at_end = ps%i > len(ps%p)
end function at_end

pure logical function failed(ps)
    type(parser_t), intent(in) :: ps
    failed = allocated(ps%errmsg)
end function failed

subroutine set_error(ps, msg, pos)
    !! Record the first error only, with its 1-based position in the pattern.
    type(parser_t), intent(inout) :: ps
    character(len=*), intent(in) :: msg
    integer, intent(in) :: pos
    if (.not. allocated(ps%errmsg)) ps%errmsg = msg//' at position '//to_str(pos)
end subroutine set_error

end module fregex_parser
