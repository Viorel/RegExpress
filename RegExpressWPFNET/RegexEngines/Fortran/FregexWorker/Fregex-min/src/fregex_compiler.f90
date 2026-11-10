module fregex_compiler
!! Compiles a parsed regular expression (AST) into a VM program.
use fregex_parser
use fregex_program
use fregex_charset, only: charset_t, full_set
use fregex_string, only: to_lower_code, is_alpha_code
implicit none
private

public :: compile_ast

integer, parameter :: MAX_PROGRAM_SIZE = 1000000
integer, parameter :: BIG_WIDTH = huge(1)/4   !! widths above this are treated as unbounded

contains

subroutine compile_ast(ast, prog, errmsg)
    type(ast_t), intent(in) :: ast
    type(program_t), intent(out) :: prog
    character(len=:), allocatable, intent(out) :: errmsg
    integer :: pc, k
    logical :: nullable
    type(charset_t) :: first

    allocate (prog%sets(-1:255, ast%nsets))
    do k = 1, ast%nsets
        prog%sets(:, k) = ast%sets(k)%table()
    end do
    prog%ngroups = ast%ngroups
    prog%names = ast%names

    pc = prog%emit(OP_SAVE, 0)
    call emit_node(ast, prog, ast%root, errmsg)
    if (allocated(errmsg)) return
    pc = prog%emit(OP_SAVE, 1)
    pc = prog%emit(OP_MATCH)

    prog%anchored = is_anchored(ast, ast%root, .false.)
    prog%line_start = .not. prog%anchored .and. is_anchored(ast, ast%root, .true.)
    call first_set(ast, ast%root, first, nullable)
    prog%first = first%table()
    prog%use_first = .not. nullable
    call literal_prefix(ast, ast%root, prog%prefix, prog%prefix_offset)
end subroutine compile_ast

recursive subroutine emit_node(ast, prog, id, errmsg)
    type(ast_t), intent(in) :: ast
    type(program_t), intent(inout) :: prog
    integer, intent(in) :: id
    character(len=:), allocatable, intent(inout) :: errmsg
    integer :: k, pc, wmin, wmax, reg
    integer, allocatable :: jumps(:)

    if (allocated(errmsg)) return
    if (prog%n > MAX_PROGRAM_SIZE) then
        errmsg = 'pattern too large after expanding repeat counts'
        return
    end if

    associate (node => ast%nodes(id))
        select case (node%kind)
        case (N_EMPTY)
            continue
        case (N_CHAR)
            if (node%b == 1) then
                pc = prog%emit(OP_CHARI, to_lower_code(node%a))
            else
                pc = prog%emit(OP_CHAR, node%a)
            end if
        case (N_ANY)
            pc = prog%emit(merge(OP_ANYNL, OP_ANY, node%a == 1))
        case (N_SET)
            pc = prog%emit(OP_SET, node%a)
        case (N_ASSERT)
            pc = prog%emit(assert_op(node%a))
        case (N_CONCAT)
            do k = 1, size(node%kids)
                call emit_node(ast, prog, node%kids(k), errmsg)
            end do
        case (N_ALT)
            ! SPLIT next, alt2; body1; JMP end; SPLIT next, alt3; body2; JMP end; ... bodyN
            allocate (jumps(size(node%kids) - 1))
            do k = 1, size(node%kids) - 1
                pc = prog%emit(OP_SPLIT, prog%n + 2)
                call emit_node(ast, prog, node%kids(k), errmsg)
                jumps(k) = prog%emit(OP_JMP)
                prog%code(2, pc) = prog%n + 1
            end do
            call emit_node(ast, prog, node%kids(size(node%kids)), errmsg)
            do k = 1, size(jumps)
                prog%code(1, jumps(k)) = prog%n + 1
            end do
        case (N_GROUP)
            pc = prog%emit(OP_SAVE, 2*node%a)
            call emit_node(ast, prog, node%kids(1), errmsg)
            pc = prog%emit(OP_SAVE, 2*node%a + 1)
        case (N_REPEAT)
            if (is_single_byte(ast, node%kids(1))) then
                call emit_single_repeat(ast, prog, node%kids(1), node%a, node%b, node%c)
            else if (node%c == R_POSSESSIVE) then
                pc = prog%emit(OP_ATOMIC)
                call emit_repeat(ast, prog, node%kids(1), node%a, node%b, .true., errmsg)
                k = prog%emit(OP_SUCCEED)
                prog%code(1, pc) = prog%n + 1
            else
                call emit_repeat(ast, prog, node%kids(1), node%a, node%b, node%c == R_GREEDY, errmsg)
            end if
        case (N_LOOK)
            if (node%a == 1) then
                pc = prog%emit(OP_LOOKAHEAD, 0, node%b)
                call emit_node(ast, prog, node%kids(1), errmsg)
            else
                call node_width(ast, node%kids(1), wmin, wmax)
                reg = prog%new_reg()
                pc = prog%emit(OP_LOOKBEHIND, 0, node%b, wmin, merge(UNBOUNDED, wmax, wmax == INF), reg)
                call emit_node(ast, prog, node%kids(1), errmsg)
                k = prog%emit(OP_ENDAT, reg)
            end if
            k = prog%emit(OP_SUCCEED)
            prog%code(1, pc) = prog%n + 1
        case (N_ATOMIC)
            pc = prog%emit(OP_ATOMIC)
            call emit_node(ast, prog, node%kids(1), errmsg)
            k = prog%emit(OP_SUCCEED)
            prog%code(1, pc) = prog%n + 1
        case (N_BACKREF)
            pc = prog%emit(OP_BACKREF, node%a, node%b)
        end select
    end associate
end subroutine emit_node

recursive subroutine emit_repeat(ast, prog, body, qmin, qmax, greedy, errmsg)
    !! Emit `body{qmin,qmax}`.
    !!
    !! The first `qmin` copies are emitted unconditionally. The rest are either a loop (unbounded) or
    !! `qmax-qmin` optional copies. If the body can match the empty string, an iteration that consumes
    !! nothing ends the repetition (as in Python's `re`), which also prevents infinite loops.
    type(ast_t), intent(in) :: ast
    type(program_t), intent(inout) :: prog
    integer, intent(in) :: body, qmin, qmax
    logical, intent(in) :: greedy
    character(len=:), allocatable, intent(inout) :: errmsg
    integer :: k, pc, loop, reg, exit_pc
    integer, allocatable :: splits(:), checks(:)
    logical :: nullable

    nullable = can_be_empty(ast, body)
    reg = 0
    if (nullable) reg = prog%new_reg()

    do k = 1, qmin
        call emit_node(ast, prog, body, errmsg)
        if (allocated(errmsg)) return
    end do

    if (qmax == INF) then
        allocate (splits(1), checks(0))
        loop = prog%emit(OP_SPLIT)
        splits(1) = loop
        call emit_iteration(ast, prog, body, reg, checks, errmsg)
        pc = prog%emit(OP_JMP, loop)
    else
        allocate (splits(qmax - qmin), checks(0))
        do k = 1, qmax - qmin
            splits(k) = prog%emit(OP_SPLIT)
            call emit_iteration(ast, prog, body, reg, checks, errmsg)
            if (allocated(errmsg)) return
        end do
    end if

    exit_pc = prog%n + 1
    do k = 1, size(splits)
        if (greedy) then
            prog%code(1:2, splits(k)) = [splits(k) + 1, exit_pc]
        else
            prog%code(1:2, splits(k)) = [exit_pc, splits(k) + 1]
        end if
    end do
    do k = 1, size(checks)
        prog%code(2, checks(k)) = exit_pc
    end do
end subroutine emit_repeat

recursive subroutine emit_iteration(ast, prog, body, reg, checks, errmsg)
    !! One optional iteration of a repeat. If `reg` is nonzero (the body can match the empty
    !! string), the iteration ends with an OP_CHECK_EXIT, whose pc is added to `checks` so that
    !! emit_repeat can point it at the end of the repeat.
    !!
    !!@note This is a module procedure rather than an internal procedure of the recursive [[emit_repeat]],
    !! because some compilers (LFortran 0.67) give an internal procedure the variables of the
    !! most recent call of its host instead of the call it was invoked from.
    type(ast_t), intent(in) :: ast
    type(program_t), intent(inout) :: prog
    integer, intent(in) :: body, reg
    integer, allocatable, intent(inout) :: checks(:)
    character(len=:), allocatable, intent(inout) :: errmsg
    integer :: pc

    if (reg /= 0) pc = prog%emit(OP_MARK, reg)
    call emit_node(ast, prog, body, errmsg)
    if (reg /= 0) then
        pc = prog%emit(OP_CHECK_EXIT, reg)
        if (allocated(checks)) then
            checks = [checks, pc]
        else
            checks = [pc]
        end if
    end if
end subroutine emit_iteration

subroutine emit_single_repeat(ast, prog, body, qmin, qmax, mode)
    !! Emit `body{qmin,qmax}` for a body that matches exactly one byte, as one OP_REPEAT. The VM
    !! then consumes the bytes in a loop and keeps one backtracking entry instead of one per byte.
    type(ast_t), intent(in) :: ast
    type(program_t), intent(inout) :: prog
    integer, intent(in) :: body, qmin, qmax, mode
    integer :: test, arg, pc

    associate (node => ast%nodes(body))
        select case (node%kind)
        case (N_CHAR)
            if (node%b == 1) then
                test = OP_CHARI
                arg = to_lower_code(node%a)
            else
                test = OP_CHAR
                arg = node%a
            end if
        case (N_ANY)
            test = merge(OP_ANYNL, OP_ANY, node%a == 1)
            arg = 0
        case default   ! N_SET
            test = OP_SET
            arg = node%a
        end select
    end associate
    pc = prog%emit(OP_REPEAT, test, arg, qmin, merge(UNBOUNDED, qmax, qmax == INF), rep_mode(mode))
end subroutine emit_single_repeat

pure integer function rep_mode(mode)
    integer, intent(in) :: mode
    select case (mode)
    case (R_LAZY)
        rep_mode = REP_LAZY
    case (R_POSSESSIVE)
        rep_mode = REP_POSSESSIVE
    case default
        rep_mode = REP_GREEDY
    end select
end function rep_mode

logical function is_single_byte(ast, id)
    !! True if node `id` always matches exactly one byte.
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    select case (ast%nodes(id)%kind)
    case (N_CHAR, N_ANY, N_SET)
        is_single_byte = .true.
    case default
        is_single_byte = .false.
    end select
end function is_single_byte

pure integer function assert_op(kind) result(op)
    integer, intent(in) :: kind
    select case (kind)
    case (A_BOL)
        op = OP_BOL
    case (A_BOL_M)
        op = OP_BOL_M
    case (A_EOL)
        op = OP_EOL
    case (A_EOL_M)
        op = OP_EOL_M
    case (A_BOS)
        op = OP_BOS
    case (A_EOS)
        op = OP_EOS
    case (A_WORDB)
        op = OP_WORDB
    case default
        op = OP_NWORDB
    end select
end function assert_op

logical function can_be_empty(ast, id)
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    integer :: wmin, wmax
    call node_width(ast, id, wmin, wmax)
    can_be_empty = wmin == 0
end function can_be_empty

recursive subroutine node_width(ast, id, wmin, wmax)
    !! Minimum and maximum number of bytes a node can match (`wmax` = INF if unbounded).
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    integer, intent(out) :: wmin, wmax
    integer :: k, kmin, kmax

    associate (node => ast%nodes(id))
        select case (node%kind)
        case (N_CHAR, N_ANY, N_SET)
            wmin = 1
            wmax = 1
        case (N_CONCAT)
            wmin = 0
            wmax = 0
            do k = 1, size(node%kids)
                call node_width(ast, node%kids(k), kmin, kmax)
                wmin = add_width(wmin, kmin)
                wmax = add_width(wmax, kmax)
            end do
        case (N_ALT)
            call node_width(ast, node%kids(1), wmin, wmax)
            do k = 2, size(node%kids)
                call node_width(ast, node%kids(k), kmin, kmax)
                wmin = min(wmin, kmin)
                if (wmax /= INF) wmax = merge(INF, max(wmax, kmax), kmax == INF)
            end do
        case (N_GROUP, N_ATOMIC)
            call node_width(ast, node%kids(1), wmin, wmax)
        case (N_REPEAT)
            call node_width(ast, node%kids(1), kmin, kmax)
            wmin = mul_width(kmin, node%a)
            if (kmax == 0 .or. node%b == 0) then
                wmax = 0
            else if (kmax == INF .or. node%b == INF) then
                wmax = INF
            else
                wmax = mul_width(kmax, node%b)
            end if
        case (N_BACKREF)
            wmin = 0
            wmax = INF
        case default   ! N_EMPTY, N_ASSERT, N_LOOK
            wmin = 0
            wmax = 0
        end select
    end associate
end subroutine node_width

pure integer function add_width(a, b) result(w)
    integer, intent(in) :: a, b
    if (a == INF .or. b == INF) then
        w = INF
    else
        w = min(a + b, BIG_WIDTH)
        if (w == BIG_WIDTH) w = INF
    end if
end function add_width

pure integer function mul_width(a, b) result(w)
    integer, intent(in) :: a, b
    if (a == 0 .or. b == 0) then
        w = 0
    else if (a > BIG_WIDTH/b) then
        w = INF
    else
        w = a*b
    end if
end function mul_width

subroutine literal_prefix(ast, id, prefix, offset)
    !! Case-sensitive literal text found at the start of every match, e.g. `ab` for `ab\d+`.
    !! Only a leading run of plain characters is recognized, which covers the common cases. A
    !! leading positive lookbehind whose body is plain characters counts too, as text just before
    !! the match: `(?<=call )\w+` gives `call ` with `offset` 5.
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    character(len=:), allocatable, intent(out) :: prefix
    integer, intent(out) :: offset
    integer :: k, kid
    integer, allocatable :: kids(:)

    prefix = ''
    offset = 0
    if (ast%nodes(id)%kind == N_CONCAT) then
        kids = ast%nodes(id)%kids
    else
        kids = [id]
    end if
    k = 1
    associate (look => ast%nodes(kids(1)))
        if (look%kind == N_LOOK .and. look%a == 0 .and. look%b == 0) then   ! (?<=...)
            if (plain_text(ast, look%kids(1), prefix)) then
                offset = len(prefix)
                k = 2
            end if
        end if
    end associate
    do k = k, size(kids)
        kid = kids(k)
        if (ast%nodes(kid)%kind /= N_CHAR .or. ast%nodes(kid)%b /= 0) exit
        prefix = prefix//achar(ast%nodes(kid)%a)
    end do
end subroutine literal_prefix

logical function plain_text(ast, id, text) result(ok)
    !! True if node `id` is one or more case-sensitive plain characters, returned in `text`.
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    character(len=:), allocatable, intent(inout) :: text
    integer :: k

    text = ''
    ok = .false.
    associate (node => ast%nodes(id))
        select case (node%kind)
        case (N_CHAR)
            if (node%b /= 0) return
            text = achar(node%a)
        case (N_CONCAT)
            do k = 1, size(node%kids)
                associate (kid => ast%nodes(node%kids(k)))
                    if (kid%kind /= N_CHAR .or. kid%b /= 0) then
                        text = ''
                        return
                    end if
                    text = text//achar(kid%a)
                end associate
            end do
        case default
            return
        end select
    end associate
    ok = .true.
end function plain_text

recursive logical function is_anchored(ast, id, lines) result(anchored)
    !! True if every match must start at the beginning of the string (`\A...` or `^...` without
    !! MULTILINE), or with `lines`, at the beginning of the string or of a line (also `^...` with
    !! MULTILINE).
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    logical, intent(in) :: lines
    integer :: k

    anchored = .false.
    associate (node => ast%nodes(id))
        select case (node%kind)
        case (N_ASSERT)
            anchored = node%a == A_BOS .or. node%a == A_BOL .or. (lines .and. node%a == A_BOL_M)
        case (N_CONCAT)
            anchored = is_anchored(ast, node%kids(1), lines)
        case (N_GROUP, N_ATOMIC)
            anchored = is_anchored(ast, node%kids(1), lines)
        case (N_REPEAT)
            anchored = node%a >= 1 .and. is_anchored(ast, node%kids(1), lines)
        case (N_ALT)
            anchored = .true.
            do k = 1, size(node%kids)
                if (.not. is_anchored(ast, node%kids(k), lines)) then
                    anchored = .false.
                    exit
                end if
            end do
        end select
    end associate
end function is_anchored

recursive subroutine first_set(ast, id, set, nullable)
    !! Add to `set` every byte a match of node `id` can start with. `nullable` is true if the
    !! node can match without consuming anything, in which case the following node's first
    !! bytes matter too. Zero-width assertions are treated as transparent, so the result is a
    !! superset, which is all the search loop needs.
    type(ast_t), intent(in) :: ast
    integer, intent(in) :: id
    type(charset_t), intent(inout) :: set
    logical, intent(out) :: nullable
    integer :: k, c
    logical :: kid_nullable
    type(charset_t) :: any_set

    associate (node => ast%nodes(id))
        select case (node%kind)
        case (N_CHAR)
            call set%add(node%a)
            if (node%b == 1) then
                c = to_lower_code(node%a)
                call set%add(c)
                call set%add(c - 32)
            end if
            nullable = .false.
        case (N_ANY)
            any_set = full_set()
            call set%add_set(any_set)
            nullable = .false.
        case (N_SET)
            call set%add_set(ast%sets(node%a))
            nullable = .false.
        case (N_CONCAT)
            nullable = .true.
            do k = 1, size(node%kids)
                call first_set(ast, node%kids(k), set, kid_nullable)
                if (.not. kid_nullable) then
                    nullable = .false.
                    exit
                end if
            end do
        case (N_ALT)
            nullable = .false.
            do k = 1, size(node%kids)
                call first_set(ast, node%kids(k), set, kid_nullable)
                nullable = nullable .or. kid_nullable
            end do
        case (N_GROUP, N_ATOMIC)
            call first_set(ast, node%kids(1), set, nullable)
        case (N_REPEAT)
            if (node%b == 0) then
                nullable = .true.
            else
                call first_set(ast, node%kids(1), set, kid_nullable)
                nullable = kid_nullable .or. node%a == 0
            end if
        case (N_BACKREF)
            any_set = full_set()
            call set%add_set(any_set)
            nullable = .true.
        case default   ! N_EMPTY, N_ASSERT, N_LOOK
            nullable = .true.
        end select
    end associate
end subroutine first_set

end module fregex_compiler
