module fregex_program
!! Compiled form of a regular expression: a list of instructions for the backtracking VM.
use fregex_string, only: regex_string_t
implicit none
private

integer, parameter, public :: NOPERANDS = 5

! Instructions. Operands are code(1:NOPERANDS, pc); "pc" values are instruction indices.
integer, parameter, public :: OP_MATCH = 1        !! whole pattern matched
integer, parameter, public :: OP_CHAR = 2         !! 1: byte
integer, parameter, public :: OP_CHARI = 3        !! 1: lowercase byte, compared case-insensitively
integer, parameter, public :: OP_ANY = 4          !! any byte except newline
integer, parameter, public :: OP_ANYNL = 5        !! any byte
integer, parameter, public :: OP_SET = 6          !! 1: index into sets
integer, parameter, public :: OP_SPLIT = 7        !! 1: preferred pc, 2: alternative pc
integer, parameter, public :: OP_JMP = 8          !! 1: pc
integer, parameter, public :: OP_SAVE = 9         !! 1: capture slot
integer, parameter, public :: OP_MARK = 10        !! 1: register; stores the current position
integer, parameter, public :: OP_CHECK_EXIT = 11  !! 1: register, 2: pc to jump to if nothing was consumed since the mark
integer, parameter, public :: OP_BOL = 12
integer, parameter, public :: OP_BOL_M = 13
integer, parameter, public :: OP_EOL = 14
integer, parameter, public :: OP_EOL_M = 15
integer, parameter, public :: OP_BOS = 16
integer, parameter, public :: OP_EOS = 17
integer, parameter, public :: OP_WORDB = 18
integer, parameter, public :: OP_NWORDB = 19
integer, parameter, public :: OP_BACKREF = 20     !! 1: group, 2: 1 if case-insensitive
integer, parameter, public :: OP_LOOKAHEAD = 21   !! 1: pc after the body, 2: 1 if negated
integer, parameter, public :: OP_LOOKBEHIND = 22  !! 1: pc after the body, 2: negated, 3: min width, 4: max width, 5: register
integer, parameter, public :: OP_ENDAT = 23       !! 1: register; succeeds only at the stored position
integer, parameter, public :: OP_ATOMIC = 24      !! 1: pc after the body
integer, parameter, public :: OP_SUCCEED = 25     !! end of a lookaround or atomic body
integer, parameter, public :: OP_REPEAT = 26      !! repeat of one byte test: 1: test op (OP_CHAR, OP_CHARI, OP_ANY,
                                                  !! OP_ANYNL or OP_SET), 2: its operand, 3: min, 4: max or
                                                  !! UNBOUNDED, 5: REP_GREEDY, REP_LAZY or REP_POSSESSIVE

integer, parameter, public :: REP_GREEDY = 0, REP_LAZY = 1, REP_POSSESSIVE = 2

integer, parameter, public :: UNBOUNDED = -1      !! lookbehind with no maximum width

type, public :: program_t
    integer :: n = 0
    integer, allocatable :: code(:, :)          !! (0:NOPERANDS, capacity); code(0, pc) is the opcode
    logical, allocatable :: sets(:, :)          !! (-1:255, nsets): byte tables of the character classes
    integer :: ngroups = 0                      !! capturing groups, not counting group 0
    integer :: nregs = 0
    type(regex_string_t), allocatable :: names(:)     !! (1:ngroups)
    logical :: anchored = .false.               !! can only match at the start of the string
    logical :: line_start = .false.             !! can only match at the start of a line (`(?m)^...`)
    logical :: use_first = .false.              !! a match must start with a byte in `first`
    logical :: first(-1:255) = .false.
    character(len=:), allocatable :: prefix     !! literal text at the start of every match (may be empty)
    integer :: prefix_offset = 0                !! how many bytes of `prefix` come before the match
                                                !! (from a leading lookbehind such as `(?<=call )`)
contains
    procedure :: emit
    procedure :: new_reg
end type program_t

contains

function emit(self, op, a, b, c, d, e) result(pc)
    !! Append an instruction and return its pc.
    class(program_t), intent(inout) :: self
    integer, intent(in) :: op
    integer, intent(in), optional :: a, b, c, d, e
    integer :: pc
    integer, allocatable :: tmp(:, :)

    if (.not. allocated(self%code)) allocate (self%code(0:NOPERANDS, 64))
    if (self%n == size(self%code, 2)) then
        allocate (tmp(0:NOPERANDS, 2*self%n))
        tmp(:, 1:self%n) = self%code(:, 1:self%n)
        call move_alloc(tmp, self%code)
    end if
    self%n = self%n + 1
    pc = self%n
    self%code(:, pc) = 0
    self%code(0, pc) = op
    if (present(a)) self%code(1, pc) = a
    if (present(b)) self%code(2, pc) = b
    if (present(c)) self%code(3, pc) = c
    if (present(d)) self%code(4, pc) = d
    if (present(e)) self%code(5, pc) = e
end function emit

integer function new_reg(self) result(reg)
    class(program_t), intent(inout) :: self
    self%nregs = self%nregs + 1
    reg = self%nregs
end function new_reg

end module fregex_program
