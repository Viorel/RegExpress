module fregex_vm
!! Backtracking virtual machine that runs a compiled program against a string.
!!
!! Choice points and the old values of captures and registers are kept on one stack. On failure
!! the stack is popped: saved values are restored (undoing the failed path) until a choice point
!! is found to resume from. Lookarounds and atomic groups run their body in a nested call.
use iso_fortran_env, only: int64
use fregex_program
use fregex_string, only: to_lower_code, is_word_code
implicit none
private

public :: vm_exec

integer, parameter, public :: MODE_SEARCH = 0     !! find the leftmost match at or after `start`
integer, parameter, public :: MODE_MATCH = 1      !! match must start at `start`
integer, parameter, public :: MODE_FULLMATCH = 2  !! match must start at `start` and end at the end of the string

integer, parameter :: BT_BRANCH = 1   !! resume at pc x, position y
integer, parameter :: BT_CAP = 2      !! restore capture slot x to y
integer, parameter :: BT_REG = 3      !! restore register x to y
integer, parameter :: BT_GREEDY = 4   !! OP_REPEAT at pc x is at position y and may give back bytes down to z
integer, parameter :: BT_LAZY = 5     !! OP_REPEAT at pc x is at position y and may take more bytes up to z

type, public :: vm_t
    !! Work space for vm_exec. It can be reused for several searches with the same program, which
    !! saves allocating it for every match in find_all, split and sub.
    private
    integer, allocatable :: caps(:)        !! (0:2*ngroups+1), positions; 0 if unset
    integer, allocatable :: regs(:)
    integer, allocatable :: stack(:, :)    !! (4, capacity): kind, x, y, z
    integer :: top = 0
    integer(int64) :: steps = 0
    integer(int64) :: step_limit = 0
    logical :: overflow = .false.
    integer :: forbid_empty_at = 0         !! reject an empty match at this position
    logical :: require_end = .false.
end type vm_t

contains

subroutine vm_exec(prog, s, start, mode, forbid_empty_at, step_limit, vm, found, caps, overflow)
    !! Run `prog` on `s`. On success `caps` holds the capture positions: slot `2k` is the first
    !! index of group k, slot `2k+1` is one past its last index, and 0 means the group did not match.
    !! `caps` is only reallocated if its size is wrong, and is left unchanged if nothing is found.
    !!
    !! `vm` is work space, which may be reused from an earlier call with the same program.
    !! `overflow` is set if the step limit was exceeded before an answer was found.
    type(program_t), intent(in) :: prog
    character(len=*), intent(in) :: s
    integer, intent(in) :: start
    integer, intent(in) :: mode
    integer, intent(in) :: forbid_empty_at
    integer(int64), intent(in) :: step_limit
    type(vm_t), intent(inout) :: vm
    logical, intent(out) :: found
    integer, allocatable, intent(inout) :: caps(:)
    logical, intent(out) :: overflow

    integer :: st, last, sp_end, k

    found = .false.
    overflow = .false.
    ! Most programs use no registers, and `caps` may have been handed to the caller last time.
    if (.not. allocated(vm%caps)) allocate (vm%caps(0:2*prog%ngroups + 1))
    if (.not. allocated(vm%stack)) allocate (vm%stack(4, 64))
    if (prog%nregs > 0 .and. .not. allocated(vm%regs)) allocate (vm%regs(prog%nregs))
    vm%steps = 0
    vm%overflow = .false.
    vm%step_limit = step_limit
    vm%forbid_empty_at = forbid_empty_at
    vm%require_end = mode == MODE_FULLMATCH

    last = len(s) + 1
    if (mode /= MODE_SEARCH) last = start
    if (prog%anchored) last = min(last, 1)

    ! A failed attempt pops everything it pushed, which restores the captures and registers, so
    ! they only need clearing once.
    vm%caps = 0
    if (allocated(vm%regs)) vm%regs = 0
    vm%top = 0
    st = max(1, start)
    do while (st <= last)
        ! skip to the next position where a match can start
        if (prog%line_start .and. st > 1) then
            if (at(s, st - 1) /= 10) then
                k = find_prefix(s, achar(10), st, last - 1)
                if (k == 0) exit
                st = k + 1
            end if
        end if
        if (len(prog%prefix) > 0) then
            ! the first `prefix_offset` bytes of the prefix come before the match
            k = find_prefix(s, prog%prefix, max(1, st - prog%prefix_offset), last - prog%prefix_offset)
            if (k == 0) exit
            st = k + prog%prefix_offset
        else if (prog%use_first) then
            do while (st <= last)
                if (prog%first(at(s, st))) exit
                st = st + 1
            end do
            if (st > last) exit
        end if
        if (run(prog, s, vm, 1, st, sp_end)) then
            found = .true.
            ! Hand over the array if the caller has none yet, which saves an allocation and
            ! a copy for one-off searches. A reused `vm` then allocates it again next time.
            if (allocated(caps)) then
                caps = vm%caps
            else
                call move_alloc(vm%caps, caps)
            end if
            return
        end if
        if (vm%overflow) then
            overflow = .true.
            return
        end if
        st = st + 1
    end do
end subroutine vm_exec

recursive function run(prog, s, vm, pc0, sp0, sp_out) result(ok)
    !! Run from instruction `pc0` at position `sp0` until OP_MATCH or OP_SUCCEED (true, with the
    !! final position in `sp_out`) or until every alternative is exhausted (false). Only stack
    !! entries pushed by this call are popped, so nested calls leave the caller's state alone.
    type(program_t), intent(in) :: prog
    character(len=*), intent(in) :: s
    type(vm_t), intent(inout) :: vm
    integer, intent(in) :: pc0, sp0
    integer, intent(out) :: sp_out
    logical :: ok

    integer :: base, pc, sp, n, c, k, t0, st, lo, hi, sub_sp, gs, ge, glen, mode
    logical :: fail, sub_ok, negated

    n = len(s)
    base = vm%top
    pc = pc0
    sp = sp0
    sp_out = sp0
    ok = .false.

    do
        vm%steps = vm%steps + 1
        if (vm%steps > vm%step_limit) then
            vm%overflow = .true.
            return
        end if

        fail = .false.
        select case (prog%code(0, pc))
        case (OP_CHAR)
            if (at(s, sp) == prog%code(1, pc)) then
                sp = sp + 1
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_CHARI)
            if (to_lower_code(at(s, sp)) == prog%code(1, pc)) then
                sp = sp + 1
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_ANY)
            c = at(s, sp)
            if (c >= 0 .and. c /= 10) then
                sp = sp + 1
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_ANYNL)
            if (sp <= n) then
                sp = sp + 1
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_SET)
            if (prog%sets(at(s, sp), prog%code(1, pc))) then
                sp = sp + 1
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_REPEAT)
            ! lo and hi: the positions the repeat may end at
            lo = sp + prog%code(3, pc)
            if (prog%code(4, pc) == UNBOUNDED) then
                hi = n + 1
            else
                hi = min(n + 1, sp + prog%code(4, pc))
            end if
            mode = prog%code(5, pc)
            if (mode == REP_LAZY) then
                k = scan_repeat(prog, pc, s, sp, lo)
                if (k < lo) then
                    fail = .true.
                else
                    if (lo < hi) call push(vm, BT_LAZY, pc, lo, hi)
                    sp = lo
                    pc = pc + 1
                end if
            else
                k = scan_repeat(prog, pc, s, sp, hi)
                if (k < lo) then
                    fail = .true.
                else
                    if (mode == REP_GREEDY .and. k > lo) call push(vm, BT_GREEDY, pc, k, lo)
                    sp = k
                    pc = pc + 1
                end if
            end if
        case (OP_SPLIT)
            call push(vm, BT_BRANCH, prog%code(2, pc), sp, 0)
            pc = prog%code(1, pc)
        case (OP_JMP)
            pc = prog%code(1, pc)
        case (OP_SAVE)
            k = prog%code(1, pc)
            call push(vm, BT_CAP, k, vm%caps(k), 0)
            vm%caps(k) = sp
            pc = pc + 1
        case (OP_MARK)
            k = prog%code(1, pc)
            call push(vm, BT_REG, k, vm%regs(k), 0)
            vm%regs(k) = sp
            pc = pc + 1
        case (OP_CHECK_EXIT)
            if (sp == vm%regs(prog%code(1, pc))) then
                pc = prog%code(2, pc)
            else
                pc = pc + 1
            end if
        case (OP_BOL, OP_BOL_M, OP_EOL, OP_EOL_M, OP_BOS, OP_EOS, OP_WORDB, OP_NWORDB)
            if (check_assertion(prog%code(0, pc), s, sp)) then
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_BACKREF)
            k = prog%code(1, pc)
            gs = vm%caps(2*k)
            ge = vm%caps(2*k + 1)
            fail = .true.
            if (gs > 0 .and. ge > 0) then
                glen = ge - gs
                if (sp + glen - 1 <= n) then
                    if (prog%code(2, pc) == 1) then
                        fail = .false.
                        do c = 0, glen - 1
                            if (to_lower_code(at(s, sp + c)) /= to_lower_code(at(s, gs + c))) then
                                fail = .true.
                                exit
                            end if
                        end do
                    else
                        fail = s(sp:sp + glen - 1) /= s(gs:ge - 1)
                    end if
                end if
            end if
            if (.not. fail) then
                sp = sp + glen
                pc = pc + 1
            end if
        case (OP_LOOKAHEAD)
            t0 = vm%top
            negated = prog%code(2, pc) == 1
            sub_ok = run(prog, s, vm, pc + 1, sp, sub_sp)
            if (vm%overflow) return
            if (sub_ok .neqv. negated) then
                if (sub_ok) call compact(vm, t0)
                pc = prog%code(1, pc)
            else
                if (sub_ok) call unwind(vm, t0)
                fail = .true.
            end if
        case (OP_LOOKBEHIND)
            t0 = vm%top
            negated = prog%code(2, pc) == 1
            k = prog%code(5, pc)
            call push(vm, BT_REG, k, vm%regs(k), 0)
            vm%regs(k) = sp
            ! try every start position allowed by the body's width, nearest first
            hi = sp - prog%code(3, pc)
            if (prog%code(4, pc) == UNBOUNDED) then
                lo = 1
            else
                lo = max(1, sp - prog%code(4, pc))
            end if
            sub_ok = .false.
            do st = hi, lo, -1
                sub_ok = run(prog, s, vm, pc + 1, st, sub_sp)
                if (vm%overflow) return
                if (sub_ok) exit
            end do
            if (sub_ok .neqv. negated) then
                if (sub_ok) then
                    call compact(vm, t0)
                else
                    call unwind(vm, t0)
                end if
                pc = prog%code(1, pc)
            else
                call unwind(vm, t0)
                fail = .true.
            end if
        case (OP_ENDAT)
            if (sp == vm%regs(prog%code(1, pc))) then
                pc = pc + 1
            else
                fail = .true.
            end if
        case (OP_ATOMIC)
            t0 = vm%top
            sub_ok = run(prog, s, vm, pc + 1, sp, sub_sp)
            if (vm%overflow) return
            if (sub_ok) then
                call compact(vm, t0)
                sp = sub_sp
                pc = prog%code(1, pc)
            else
                fail = .true.
            end if
        case (OP_SUCCEED)
            ok = .true.
            sp_out = sp
            return
        case (OP_MATCH)
            if (vm%require_end .and. sp /= n + 1) then
                fail = .true.
            else if (sp == vm%forbid_empty_at .and. vm%caps(0) == sp) then
                fail = .true.
            else
                ok = .true.
                sp_out = sp
                return
            end if
        end select

        if (fail) then
            ! backtrack: undo saved values until a choice point is found
            do
                if (vm%top <= base) return
                k = vm%top
                vm%top = vm%top - 1
                select case (vm%stack(1, k))
                case (BT_BRANCH)
                    pc = vm%stack(2, k)
                    sp = vm%stack(3, k)
                    exit
                case (BT_GREEDY)   ! give back one byte, keeping the entry while more can be given back
                    pc = vm%stack(2, k) + 1
                    sp = vm%stack(3, k) - 1
                    if (prog%code(0, pc) == OP_CHAR) then
                        ! give back straight to where the literal byte that follows can match
                        c = prog%code(1, pc)
                        do while (sp >= vm%stack(4, k))
                            if (at(s, sp) == c) exit
                            sp = sp - 1
                        end do
                        if (sp < vm%stack(4, k)) cycle   ! nowhere: drop the entry
                    end if
                    if (sp > vm%stack(4, k)) then
                        vm%stack(3, k) = sp
                        vm%top = k
                    end if
                    exit
                case (BT_LAZY)   ! take one more byte if it matches, else keep backtracking
                    sp = vm%stack(3, k)
                    if (byte_matches(prog, vm%stack(2, k), at(s, sp))) then
                        pc = vm%stack(2, k) + 1
                        sp = sp + 1
                        if (sp < vm%stack(4, k)) then
                            vm%stack(3, k) = sp
                            vm%top = k
                        end if
                        exit
                    end if
                case (BT_CAP)
                    vm%caps(vm%stack(2, k)) = vm%stack(3, k)
                case (BT_REG)
                    vm%regs(vm%stack(2, k)) = vm%stack(3, k)
                end select
            end do
        end if
    end do
end function run

pure logical function check_assertion(op, s, sp) result(ok)
    integer, intent(in) :: op
    character(len=*), intent(in) :: s
    integer, intent(in) :: sp
    integer :: n

    n = len(s)
    select case (op)
    case (OP_BOL, OP_BOS)
        ok = sp == 1
    case (OP_BOL_M)
        ok = sp == 1 .or. at(s, sp - 1) == 10
    case (OP_EOL)
        ok = sp == n + 1 .or. (sp == n .and. at(s, sp) == 10)
    case (OP_EOL_M)
        ok = sp == n + 1 .or. at(s, sp) == 10
    case (OP_EOS)
        ok = sp == n + 1
    case (OP_WORDB)
        ok = is_word_code(at(s, sp - 1)) .neqv. is_word_code(at(s, sp))
    case default   ! OP_NWORDB; like Python, never matches in an empty string
        ok = n > 0 .and. (is_word_code(at(s, sp - 1)) .eqv. is_word_code(at(s, sp)))
    end select
end function check_assertion

pure integer function at(s, i) result(c)
    !! Byte value (0-255) of `s(i:i)`, or -1 if `i` is outside the string. The same as
    !! fregex_string's byte_at, but in this module so that the compiler can inline it.
    character(len=*), intent(in) :: s
    integer, intent(in) :: i
    if (i < 1 .or. i > len(s)) then
        c = -1
    else
        c = iand(ichar(s(i:i)), 255)
    end if
end function at

pure integer function find_prefix(s, prefix, from, last) result(pos)
    !! The first position in `from:last` where `prefix` occurs in `s`, or 0 if there is none.
    !! This looks for the first byte in a tight loop and only then compares the rest, which is
    !! several times faster than gfortran's `index`.
    character(len=*), intent(in) :: s, prefix
    integer, intent(in) :: from, last
    integer :: np
    np = len(prefix)
    do pos = from, min(last, len(s) - np + 1)
        if (s(pos:pos) == prefix(1:1)) then
            if (s(pos:pos + np - 1) == prefix) return
        end if
    end do
    pos = 0
end function find_prefix

pure logical function byte_matches(prog, pc, c) result(ok)
    !! True if byte `c` (-1 at the end of the string) passes the test of the OP_REPEAT at `pc`.
    type(program_t), intent(in) :: prog
    integer, intent(in) :: pc, c
    select case (prog%code(1, pc))
    case (OP_CHAR)
        ok = c == prog%code(2, pc)
    case (OP_CHARI)
        ok = to_lower_code(c) == prog%code(2, pc)
    case (OP_ANY)
        ok = c >= 0 .and. c /= 10
    case (OP_ANYNL)
        ok = c >= 0
    case default   ! OP_SET
        ok = prog%sets(c, prog%code(2, pc))
    end select
end function byte_matches

pure integer function scan_repeat(prog, pc, s, from, upto) result(p)
    !! The first position in `from:upto-1` whose byte fails the test of the OP_REPEAT at `pc`,
    !! or `upto` if they all pass.
    type(program_t), intent(in) :: prog
    integer, intent(in) :: pc, from, upto
    character(len=*), intent(in) :: s
    integer :: b
    p = from
    b = prog%code(2, pc)
    select case (prog%code(1, pc))
    case (OP_SET)
        do while (p < upto)
            if (.not. prog%sets(at(s, p), b)) exit
            p = p + 1
        end do
    case (OP_CHAR)
        do while (p < upto)
            if (at(s, p) /= b) exit
            p = p + 1
        end do
    case (OP_ANYNL)
        p = max(from, min(upto, len(s) + 1))
    case default
        do while (p < upto)
            if (.not. byte_matches(prog, pc, at(s, p))) exit
            p = p + 1
        end do
    end select
end function scan_repeat

pure subroutine push(vm, kind, x, y, z)
    type(vm_t), intent(inout) :: vm
    integer, intent(in) :: kind, x, y, z
    integer, allocatable :: tmp(:, :)

    if (vm%top == size(vm%stack, 2)) then
        allocate (tmp(4, 2*vm%top))
        tmp(:, 1:vm%top) = vm%stack(:, 1:vm%top)
        call move_alloc(tmp, vm%stack)
    end if
    vm%top = vm%top + 1
    vm%stack(1, vm%top) = kind
    vm%stack(2, vm%top) = x
    vm%stack(3, vm%top) = y
    vm%stack(4, vm%top) = z
end subroutine push

pure subroutine unwind(vm, t0)
    !! Pop everything above `t0`, restoring saved values.
    type(vm_t), intent(inout) :: vm
    integer, intent(in) :: t0
    integer :: k
    do k = vm%top, t0 + 1, -1
        select case (vm%stack(1, k))
        case (BT_CAP)
            vm%caps(vm%stack(2, k)) = vm%stack(3, k)
        case (BT_REG)
            vm%regs(vm%stack(2, k)) = vm%stack(3, k)
        end select
    end do
    vm%top = t0
end subroutine unwind

pure subroutine compact(vm, t0)
    !! Drop the choice points above `t0` but keep the saved values, so that a successful
    !! lookaround or atomic group cannot be re-entered, yet its effects can still be undone.
    type(vm_t), intent(inout) :: vm
    integer, intent(in) :: t0
    integer :: k, j
    j = t0
    do k = t0 + 1, vm%top
        if (vm%stack(1, k) == BT_CAP .or. vm%stack(1, k) == BT_REG) then
            j = j + 1
            vm%stack(:, j) = vm%stack(:, k)
        end if
    end do
    vm%top = j
end subroutine compact

end module fregex_vm
