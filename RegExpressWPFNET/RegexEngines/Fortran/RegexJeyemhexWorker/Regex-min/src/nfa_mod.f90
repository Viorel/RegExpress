module nfa_mod
!==============================================================================#
! NFA_MOD
!------------------------------------------------------------------------------#
! Author:  Ed Higgins <ed.j.higgins@gmail.com>
!------------------------------------------------------------------------------#
! Version: 0.1.1, 2024-09-05
!------------------------------------------------------------------------------#
! This code is distributed under the MIT license.
!==============================================================================#
  use utils_mod
  use states_mod
  use postfix_mod
  implicit none

  private
  public :: build_nfa, allocate_nfa, deallocate_nfa, run_nfa_fast, run_nfa_full, print_nfa_state

  integer,  parameter ::  nfa_max_print   = 16    ! Maximum depth for print_state
  logical,  parameter :: nfa_debug = .false.

  ! Full NFA and list of states
  type, public :: nfa_type
    type(state),    pointer :: head             ! Starting state for the NFA
    type(ptr_list), pointer :: states => null() ! A list of all the states in this nfa
    integer                 :: n_states         ! Number of states in the NFA
  end type nfa_type

  ! State in the NFA
  type, public  :: state
    integer               ::  c                 ! Character/code to match
    type(state),  pointer ::  out1 => null()    ! Optional output 1 from the state
    type(state),  pointer ::  out2 => null()    ! Optional output 1 from the state
    integer               ::  last_list         ! State list tracker for fast NFA running
  end type state

  ! List of pointers to states
  type  :: ptr_list
    type(state),    pointer ::  s    => null()  ! The state
    integer                 ::  side =  -1      ! Is this the left or right side of a branch?
    type(ptr_list), pointer ::  next => null()  ! Next state in the list
    integer                 ::  refs =  0       ! Number of references to this list item
  end type ptr_list

  ! NFA fragment
  type  :: frag
    type(state),    pointer ::  start => null() ! Starting state of the fragment
    type(ptr_list), pointer ::  out1  => null() ! List of all output states from the fragment
  end type frag

  ! Fragment stack node
  type  :: frag_stack
    type(frag), pointer ::  elem
  end type frag_stack

contains

  !------------------------------------------------------------------------------!
    function build_nfa(postfix) result(nfa)                                      !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to convert a postfix expression to a Nondeterministic Finite       !
  !   Automaton, with the head stored in state 'states'                          !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   integer, intent(in) :: postfix(pf_buff_size)                               !
  !     Postfix expression stored as an array of integers                        !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   type(nfa)                                                                  !
  !     Resultant NFA                                                            !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    type(nfa_type)  ::  nfa
    integer,  intent(in)  ::  postfix(pf_buff_size)

    integer ::  pf_loc, s_loc
    type(frag_stack), allocatable ::  stack(:)            ! Stack of unassigned NFA fragments
    type(frag_stack), allocatable ::  allocated_frags(:)  ! Stack of all allocated NFA fragments
    type(frag),     pointer ::  stack_p                   ! Pointer to the top of the stack
    type(frag),     pointer ::  e1, e2, e                 ! Pointers to fragments being operated on 
    type(state),    pointer ::  s                         ! Pointer to a state
    type(state),    pointer ::  matchstate                ! The "Successful match" state
    type(state),    pointer ::  nullstate                 ! The "Nothing to be pointed to" state

    integer ::  nfrags, i, ierr

    call allocate_nfa(nfa)

    ! Allocate and initalise the appropriate datastructures
    nfrags = 0
    allocate(allocated_frags(pf_stack_size), stat=ierr)
    if (ierr /= 0) call throw_error("Unable to allocate frag stack")
    allocate(stack(pf_stack_size),stat=ierr)
    if (ierr /= 0) call throw_error("Unable to allocate stack")

    do i = 1, pf_stack_size
      stack(i)%elem => null()
      allocated_frags(i)%elem => null()
    end do

    if (nfa%states%side /= 0) call throw_error("Trying to build nfa with in-use states")

    ! Allocate the Match and Null states for this NFA
    matchstate => new_state(match_st, null(), null())
    nullstate => new_state(null_st, null(), null())

    stack_p => stack(1)%elem
    pf_loc  = 1
    s_loc   = 1

    ! While there are still states in the postfix list:
    do while (postfix(pf_loc) /= null_st)
      s => null()
      select case( postfix(pf_loc) )

      case(cat_op) ! A concatanation operation
        ! Pop the top two fragments off the stack (e1 and e2)
        e2 => pop()
        e1 => pop()

        ! Patch the ends of e1 to the start of e2
        call patch(e1%out1, e2%start)

        ! Push the resultant fragment, starting at e1, back onto the stack
        call push(new_frag(e1%start, e2%out1))
        e1 => null()
        e2 => null()

      case(or_op) ! A '|' operation
        ! Pop the top two fragments off the stack (e1 and e2)
        e2 => pop()
        e1 => pop()

        ! Create a split state pointing to the start of e1 and e2
        s => new_state( split_st, e1%start, e2%start )

        ! Append the outputs of e2 to the output list of e1
        call append(e1%out1, e2%out1)

        ! Push a new fragment, starting at s, back onto the stack
        call push( new_frag(s, e1%out1) )
        e1 => null()
        e2 => null()

      case(quest_op) ! A '?' operation
        ! Pop the top fragment off the stack (e) and create a split state (s)
        e => pop()
        s => new_state( split_st, e%start, nullstate )

        ! Create a new state list contining s and append the outputs to the output list of e
        call append(e%out1, new_list(s,2))

        ! Push a new fragment, starting at s, onto the stack
        call push( new_frag(s, e%out1) )
        e => null()

      case(star_op) ! A '*' operation
        ! Pop the top fragment off the stack (e) and create a split state (s)
        e => pop()
        s => new_state( split_st, e%start, nullstate )

        ! Patch the ends of s to the start of e
        call patch(e%out1, s)

        ! Push a new fragment, starting at s, onto the stack
        call push( new_frag(s, new_list(s, 2))  )
        e => null()

      case(plus_op) ! A '+' operation
        ! Pop the top fragment off the stack (e) and create a split state (s)
        e => pop()
        s => new_state( split_st, e%start, nullstate )

        ! Patch the ends of s to the start of e
        call patch(e%out1, s)

        ! Push a new fragment, starting at e, onto the stack
        call push( new_frag(e%start, new_list(s, 2))  )
        e => null()

      case default ! Everything else
        ! Create a new state for this particular character
        s => new_state( postfix(pf_loc), nullstate, nullstate )

        ! Push a new fragment, starting at e, onto the stack
        call push( new_frag(s, new_list(s, 1)) )
        e => null()

      end select

      ! Advance to the next postfix token
      pf_loc = pf_loc + 1
    end do

    ! Pop off (hopefully) the final element on the stack
    e => pop()
    if (s_loc /= 1) call throw_warning("Stack is not empty on exit")
    call patch(e%out1, matchstate)

    nfa%head => e%start

    ! If we've messed up, matchstate or nullstate might have changed; check for this
    if (matchstate%c /= match_st) call throw_warning("***** Matchstate has changed!")
    if (nullstate%c /= null_st) call throw_warning("***** Nullstate has changed!")

    ! Deallocate the memory we used along the way
    do i = 1, nfrags
      if (associated(allocated_frags(i)%elem)) then
        call deallocate_list(allocated_frags(i)%elem%out1, keep_states=.true.)
        if (associated(allocated_frags(i)%elem%start)) allocated_frags(i)%elem%start => null()
        deallocate(allocated_frags(i)%elem, stat=ierr)
        if (ierr /= 0) call throw_warning("Unable to deallocate fragment")
        allocated_frags(i)%elem => null()
      end if
    end do

    deallocate(stack, allocated_frags, stat=ierr)
    if (ierr /= 0) call throw_warning("Unable to deallocate stacks")
    e => null()

  contains

    !------------------------------------------------------------------------------!
      function new_frag(s, l)                                                      !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to create a new NFA fragment.                                      !
    !------------------------------------------------------------------------------!
      type(frag), pointer ::  new_frag
      type(state),    pointer,  intent(in)  ::  s
      type(ptr_list), pointer,  intent(in)  ::  l

      allocate(new_frag)
      new_frag%start => s
      call point_list(new_frag%out1, l)

      nfrags = nfrags + 1
      allocated_frags(nfrags)%elem => new_frag

    end function new_frag

    !------------------------------------------------------------------------------!
      function new_state(c, out1, out2)                                            !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to create a new NFA state, outputting to out1 and out2.            !
    !------------------------------------------------------------------------------!
      type(state), pointer  ::  new_state
      integer,                intent(in)  ::  c
      type(state),  pointer,  intent(in)  ::  out1, out2

      integer ::  ierr

      new_state => null()
      allocate(new_state, stat=ierr)
      if (ierr /= 0) call throw_error("Unable to allocate new_state")
      new_state%last_list = 0
      new_state%c = c
      new_state%out1 => out1
      new_state%out2 => out2

      call append(nfa%states, new_list(new_state, -1))
      nfa%n_states = nfa%n_states + 1

    end function new_state

    !------------------------------------------------------------------------------!
      subroutine push(f)                                                           !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to push NFA fragment onto the stack.                               !
    !------------------------------------------------------------------------------!
      type(frag), intent(in), pointer  ::  f

      s_loc = s_loc + 1
      stack(s_loc)%elem => f

    end subroutine push

    !------------------------------------------------------------------------------!
      function pop()                                                               !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to push an NFA off the stack, and returning it.                    !
    !------------------------------------------------------------------------------!
      type(frag), pointer :: pop

      pop => stack(s_loc)%elem
      s_loc = s_loc - 1

    end function pop

  end function build_nfa

  !------------------------------------------------------------------------------!
    function run_nfa_fast(nfa, str, start, finish) result(res)                   !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to simulate the NFA 'nfa' o n the string 'str', starting 'start'   !
  !   characters in. This routine uses the fast algorithm. This algorithm        !
  !   doesn't allow submatching.                                                 !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(nfa_type),   intent(inout)             ::  nfa                        !
  !     NFA to be simulated                                                      !
  !                                                                              !
  !   character(len=*), intent(in)                ::  str                        !
  !     String to be searched                                                    !
  !                                                                              !
  !   integer,          intent(inout)             ::  start                      !
  !     Where in str to start. On exit, returns the start of the match if        !
  !     matched                                                                  !
  !                                                                              !
  !   integer,          intent(out),    optional  ::  finish                     !
  !     Last character of matched string                                         !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   TRUE if there is a match, FALSE otherwise.                                 !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    logical :: res
    type(nfa_type),   intent(inout)             ::  nfa
    character(len=*), intent(in)                ::  str
    integer,          intent(inout)             ::  start
    integer,          intent(out),    optional  ::  finish

    type  ::  list
      type(state),  pointer ::  s
    end type list

    type(list), allocatable, target ::  l1(:), l2(:)
    integer                         ::  list_id = 0
    integer ::  loc_start
    logical ::  no_advance

    type(list), pointer ::  c_list(:), n_list(:), t(:)
    integer ::  ch_loc, n_cl, n_nl, n_t
    integer ::  istart, i, ierr

    allocate(l1(1:nfa%n_states), l2(1:nfa%n_states), stat=ierr)
    if (ierr /= 0) call throw_error("Error allocating l1,l2 in run_nfa_fast")

    ! The match might not start on the first character of the string,
    !   test the NFA starting on each character until we find a match
    start_loop: do istart = start, len(str)

      ! Initialise the variables for a new run
      do i = 1, nfa%n_states
        l1(i)%s => null()
        l2(i)%s => null()
      end do

      n_cl = 1
      n_nl = 1

      c_list => start_list(l1, n_cl, nfa%head)
      n_list => l2

      ch_loc = istart
      loc_start = istart

      res = .false.

      if (present(finish)) finish = -1

      ! If the first character matches, we're done!
      if ( is_match(c_list, n_cl) ) then
        res = .true.
        if (present(finish)) finish = min(ch_loc, len(str))
      end if

      ! Keep trying to match until we hit the end of the string
      do while (ch_loc <= len(str)+1)
        no_advance  = .false.

        ! Step each possible path through the NFA
        call step()

        ! Swap the current and next lists wround
        t      => c_list
        c_list => n_list
        n_list => t
        n_t  = n_cl
        n_cl = n_nl
        n_nl = n_t

        ! If any of the new current list match, make a note of that
        if ( is_match(c_list, n_cl) ) then
          res = .true.
          if (present(finish)) finish = min(ch_loc, len(str))
        end if

        ! Possibly advance to the next character in the string (not if we're matching, e.g. '^')
        if (.not. no_advance) ch_loc = ch_loc + 1
      end do

      ! Return if we've found a match
      if (res) exit start_loop
    end do start_loop

    ! If we matched, store the start of the match
    if (res) start = loc_start
    deallocate(l1, l2)

  contains

    !------------------------------------------------------------------------------!
      function start_list(l, n_l, s)                                               !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to initialise a list of active states.                             !
    !------------------------------------------------------------------------------!
      type(list), pointer ::  start_list(:)
      type(list),   target,   intent(inout)  ::  l(:)
      integer,                intent(inout)  ::  n_l
      type(state),  pointer,  intent(inout)  ::  s


      n_l = 1
      list_id = list_id + 1
      start_list => l

      call add_state(start_list, n_l, s)

    end function start_list

    !------------------------------------------------------------------------------!
      subroutine step()                                                            !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to step through one node of the NFA for each state in the current  !
    !   list.                                                                      !
    !------------------------------------------------------------------------------!
      integer ::  i
      type(state),  pointer ::  s => null()

      list_id = list_id + 1
      n_nl = 1

      do i=1, n_cl-1
        s => c_list(i)%s

        if (ch_loc <= len(str)) then
          select case(s%c)

            case(0:255)
              if ( s%c == iachar(str(ch_loc:ch_loc)) ) then
                call add_state(n_list, n_nl, s%out1)
              end if

            case(any_ch)
              call add_state(n_list, n_nl, s%out1)
            case(alpha_ch)
              select case( str(ch_loc:ch_loc) )
                case("a":"z","A":"Z")
                  call add_state(n_list, n_nl, s%out1)
              end select
            case(numeric_ch)
              select case( str(ch_loc:ch_loc) )
                case("0":"9")
                  call add_state(n_list, n_nl, s%out1)
              end select
            case(word_ch)
              select case( str(ch_loc:ch_loc) )
                case("a":"z","A":"Z","0":"9","_")
                  call add_state(n_list, n_nl, s%out1)
              end select
            case(space_ch)
              select case( str(ch_loc:ch_loc) )
                case(" ", achar(9), achar(10))
                  call add_state(n_list, n_nl, s%out1)
              end select

            case(n_alpha_ch)
              select case( str(ch_loc:ch_loc) )
                case("a":"z","A":"Z")
                case default
                  call add_state(n_list, n_nl, s%out1)
              end select
            case(n_numeric_ch)
              select case( str(ch_loc:ch_loc) )
                case("0":"9")
                case default
                  call add_state(n_list, n_nl, s%out1)
              end select
            case(n_word_ch)
              select case( str(ch_loc:ch_loc) )
                case("a":"z","A":"Z","0:9","_")
                case default
                  call add_state(n_list, n_nl, s%out1)
              end select
            case(n_space_ch)
              select case( str(ch_loc:ch_loc) )
                case(" ", achar(9), achar(10))
                case default
                  call add_state(n_list, n_nl, s%out1)
              end select

            case(start_ch)
              if (ch_loc == 1) call add_state(n_list, n_nl, s%out1)
              no_advance = .true.

            case(open_par_ch)
              call add_state(n_list, n_nl, s%out1)
              no_advance = .true.

            case(close_par_ch)
              call add_state(n_list, n_nl, s%out1)
              no_advance = .true.

            case(finish_ch)

            case( match_st )

            case default
              call throw_error("Unrecognised state " // achar(s%c))
          end select
        else
          if (s%c == finish_ch) then
            call add_state(n_list, n_nl, s%out1)
          end if
        end if
      end do

    end subroutine step

    !------------------------------------------------------------------------------!
      function is_match(l, n_l)                                                    !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to check if any nodes in the list l are match states in the NFA.   !
    !------------------------------------------------------------------------------!
      logical ::  is_match
      type(list), pointer,  intent(in)  ::  l(:)
      integer,              intent(in)  ::  n_l

      integer ::  i

      do i = 1, n_l-1
        if ( l(i)%s%c == match_st ) then
          is_match = .true.
          return
        end if
      end do
      is_match = .false.

    end function is_match

    !------------------------------------------------------------------------------!
      recursive subroutine add_state(l, n_l, s)                                    !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to add the state s to the end of list l. If s is a split_st, add   !
    !   its output instead.                                                        !
    !------------------------------------------------------------------------------!
      type(list),   pointer,  intent(inout) ::  l(:)
      integer,                intent(inout) ::  n_l
      type(state),  pointer,  intent(inout) ::  s

      if ( (s%c == null_st) .or. (s%last_list == list_id) ) return
      s%last_list = list_id
      if (s%c == split_st) then
        call add_state(l, n_l, s%out1)
        call add_state(l, n_l, s%out2)
        return
      end if
      l(n_l)%s => s
      n_l = n_l + 1

    end subroutine add_state

  end function run_nfa_fast

  !------------------------------------------------------------------------------!
    recursive function run_nfa_full(nfa, str, start, finish, s_in) result(res)   !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to simulate the NFA 'nfa' o n the string 'str', starting 'start'   !
  !   characters in. This routine uses the slower algorithm. This algorithm      !
  !   does allow submatching.                                                    !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(nfa_type),       intent(inout)           ::  nfa                      !
  !     NFA to be simulated                                                      !
  !                                                                              !
  !   character(len=*),     intent(in)              ::  str                      !
  !     String to be searched                                                    !
  !                                                                              !
  !   integer,              intent(inout)           ::  start                    !
  !     Where in str to start. On exit, returns the start of the match if        !
  !     matched                                                                  !
  !                                                                              !
  !   integer,              intent(out),  optional  ::  finish                   !
  !     Last character of matched string                                         !
  !                                                                              !
  !   type(state), pointer, intent(in),   optional  ::  s_in                     !
  !     Node to start on  is the start of the NFA or not.                        !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   TRUE if there is a match, FALSE otherwise.                                 !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    logical :: res
    type(nfa_type),       intent(inout)           ::  nfa
    character(len=*),     intent(in)              ::  str
    integer,              intent(inout)           ::  start
    integer,              intent(out),  optional  ::  finish
    type(state), pointer, intent(in),   optional  ::  s_in

    type(state), pointer :: s
    integer ::  istart, fin

    res = .false.
    if (present(finish)) finish = -1
    fin = -1


    if (present(s_in)) then
      istart = start
      s => s_in
      if (nfa_debug) write(*,*) "Checking " // state_str(s%c) // " against " // str(start:start)
      call step()
    else
      start_loop: do istart = start, len(str)
        s => nfa%head
        if (nfa_debug) write(*,*) "Checking " // state_str(s%c) // " against " // str(start:start)
        call step()
        if (res) exit start_loop
      end do start_loop
    end if

    if (present(finish)) then
      if (finish == -1) finish = fin
    end if
    start = istart

    if (nfa_debug) write(*,*) "res = ", res, start, finish

  contains

    !------------------------------------------------------------------------------!
      recursive subroutine step()                                                  !
    !------------------------------------------------------------------------------!
    ! DESCRPTION                                                                   !
    !   Routine to step through the NFA. If it does not reach an end, run_nfa_full !
    !   is re-called.                                                              !
    !------------------------------------------------------------------------------!
      integer ::  next_start
      integer :: local_istart

      local_istart = istart
      next_start = -1
      if (local_istart <= len(str)) then
        select case(s%c)
          case( match_st )
            res = .true.
            if (present(finish)) finish = local_istart-1

          case( split_st )
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)
            if (.not. res) res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out2)

          case(0:255)
            if ( s%c == iachar(str(local_istart:local_istart)) ) then
              next_start = local_istart + 1
              res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end if

          case(any_ch)
            next_start = local_istart + 1
            res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
          case(alpha_ch)
            select case( str(local_istart:local_istart) )
              case("a":"z","A":"Z")
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select
          case(numeric_ch)
            select case( str(local_istart:local_istart) )
              case("0":"9")
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select
          case(word_ch)
            select case( str(local_istart:local_istart) )
              case("a":"z","A":"Z","0":"9","_")
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select
          case(space_ch)
            select case( str(local_istart:local_istart) )
              case(" ", achar(9), achar(10))
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select

          case(n_alpha_ch)
            select case( str(local_istart:local_istart) )
              case("a":"z","A":"Z")
              case default
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select
          case(n_numeric_ch)
            select case( str(local_istart:local_istart) )
              case("0":"9")
              case default
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select
          case(n_word_ch)
            select case( str(local_istart:local_istart) )
              case("a":"z","A":"Z","0:9","_")
              case default
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select
          case(n_space_ch)
            select case( str(local_istart:local_istart) )
              case(" ", achar(9), achar(10))
              case default
                next_start = local_istart + 1
                res = run_nfa_full(nfa, str, next_start, fin, s_in = s%out1)
            end select

          case(start_ch)
            if (start == 1) res = run_nfa_full(nfa, str, start, fin, s_in = s%out1)

          case(open_par_ch)
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)

          case(close_par_ch)
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)

          case(finish_ch)

          case default
            call throw_error("Unrecognised state " // achar(s%c))
        end select
      else
        select case(s%c)
          case(open_par_ch)
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)

          case(close_par_ch)
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)

          case( split_st )
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)
            if (.not. res) res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out2)
          case( match_st )
            res = .true.
            if (present(finish)) finish = len(str)
          case( finish_ch )
            res = run_nfa_full(nfa, str, local_istart, fin, s_in = s%out1)
        end select
      end if

    end subroutine step

  end function run_nfa_full


  !------------------------------------------------------------------------------!
    recursive subroutine print_nfa_state(s, depth)                                   !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to print out an NFA state in a human readable manner. It is        !
  !   recursively called on all outputs of the state until nfa_max_print is      !
  !   reached.                                                                   !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(state), pointer, intent(in) :: s                                      !
  !     State to be printed                                                      !
  !                                                                              !
  !   integer, optional ,   intent(in) :: depth = 0                              !
  !     Depth of the state into the NFA                                          !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    type(state), pointer, intent(in) ::  s
    integer,  optional,   intent(in) :: depth

    integer ::  local_depth, i
    type(state), pointer  ::  tmp_s

    local_depth=0
    if (present(depth)) then
      local_depth = depth
    end if

    ! Limit depth of print, mostly to avoid infinite loops
    if (local_depth > nfa_max_print) then
      print *, "Trying to print a superdeep structure!"
    else
      tmp_s => s
      if (tmp_s%c /= null_st) then
        ! Make sure the state is properly indeneted
        do i = 1, local_depth
          write(*,'(A3)', advance="no") "|  "
        end do
        write(*,'(A7,A5)') state_str(tmp_s%c)
      end if

      ! if the state has any output states, print them too
      if (associated(tmp_s%out1)) call print_nfa_state(tmp_s%out1, depth=local_depth+1)
      if (associated(tmp_s%out2)) call print_nfa_state(tmp_s%out2, depth=local_depth+1)
    end if

  end subroutine print_nfa_state

  !------------------------------------------------------------------------------!
    function new_list(outp, side)                                                !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to create a new state list, with outp as the first state.          !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(state),    pointer,  intent(in)  ::  outp                             !
  !     First NFA state in the list                                              !
  !                                                                              !
  !   integer,                  intent(in)  ::  side                             !
  !     Which side of the the state goes on                                      !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   type(ptr_list), pointer                                                    !
  !     Pointer to the newly created list                                        !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    type(ptr_list), pointer :: new_list
    type(state),    pointer,  intent(in)  ::  outp
    integer,                  intent(in)  ::  side

    integer ::  ierr

    new_list => null()

    allocate(new_list, stat=ierr)
    if (ierr /= 0) call throw_error("Unable to allocate new_list")

    new_list%s    => outp
    new_list%side =  side
    new_list%next => null()
    new_list%refs =  0

  end function new_list

  !------------------------------------------------------------------------------!
    recursive subroutine nullify_list(l)                                         !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   A routine to nullify a ptr_list. If the list is left unreferenced,         !
  !   also deallocate it.                                                        !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(ptr_list),    pointer,  intent(in)  ::  l                             !
  !     The list to be nullified                                                 !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2017-12-04                                                 !
  !------------------------------------------------------------------------------!
    type(ptr_list), pointer, intent(inout) :: l

    if (associated(l)) then
      l%refs=l%refs-1

      if(l%refs == 0) then
        if(associated(l%next)) call nullify_list(l%next)
        deallocate(l)
      end if

      l => null()
    end if

  end subroutine nullify_list

  !------------------------------------------------------------------------------!
    subroutine point_list(l1, l2)                                                !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   A routine to point one ptr_list at another (l1 => l2), whilst also keeping !
  !   track of how many references each list has pointing to it.                 !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(ptr_list),    pointer,  intent(inout)  :: l1                          !
  !     The list to be nullified                                                 !
  !   type(ptr_list),    pointer,  intent(in)     :: l2                          !
  !     The list to be nullified                                                 !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2017-12-04                                                 !
  !------------------------------------------------------------------------------!
    type(ptr_list), pointer, intent(inout)  :: l1
    type(ptr_list), pointer, intent(in)     :: l2

    if(associated(l1)) call nullify_list(l1)

    if(associated(l2)) then
      l1 => l2
      l1%refs = l1%refs + 1
    endif

  end subroutine point_list

  !------------------------------------------------------------------------------!
    subroutine append(l1, l2)                                                    !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to append ptr_list l2 to the end of ptr_list l1.                   !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(ptr_list), pointer,  intent(inout) :: l1                              !
  !     list to be appended to                                                   !
  !                                                                              !
  !   type(ptr_list), pointer,  intent(in)    :: l2                              !
  !     list to be appended                                                      !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   type(ptr_list), pointer                                                    !
  !     resultant list                                                           !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    type(ptr_list), pointer,  intent(inout) :: l1
    type(ptr_list), pointer,  intent(in)    :: l2

    type(ptr_list), pointer :: tmp_l

    tmp_l => null()

    call point_list(tmp_l, l1)
    do while ( associated(tmp_l%next) )
      call point_list(tmp_l, tmp_l%next)
    end do

    call point_list(tmp_l%next, l2)

    call nullify_list(tmp_l)

  end subroutine append

  !------------------------------------------------------------------------------!
    subroutine deallocate_list(l, keep_states, n_states)                         !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to deallocate a ptr_list and, optionally, the NFA states in it.    !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(ptr_list), pointer,  intent(inout) ::  l                              !
  !     Pointer list to be deallocated                                           !
  !                                                                              !
  !   logical,        optional, intent(in)    ::  keep_states = false            !
  !     Whether or not the states within the list should be deallocated as well  !
  !                                                                              !
  !   integer,        optional, intent(inout) ::  n_states = 0                   !
  !     Number of allocated states in the list                                   !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    type(ptr_list), pointer,  intent(inout) ::  l
    logical,        optional, intent(in)    ::  keep_states
    integer,        optional, intent(inout) ::  n_states

    type(ptr_list), pointer ::  tmp_l
    logical ::  local_ks
    integer ::  ierr

    tmp_l => null()
    local_ks = .false.
    if (present(keep_states)) local_ks = keep_states

    if (.not. associated(l)) return

    do while (associated(l%next))
      call point_list(tmp_l, l)
      call point_list(l, tmp_l%next)
      if ((associated(tmp_l%s)) .and. (.not. local_ks)) then
        deallocate(tmp_l%s)
        if (present(n_states)) n_states = n_states - 1
      else
        tmp_l%s => null()
      end if
      call nullify_list(tmp_l)
    end do

    if ((associated(l%s)) .and. (.not. local_ks)) then
      deallocate(l%s, stat=ierr)
      if (ierr /= 0) call throw_warning("Unable to deallocate l%s")
      if (present(n_states)) n_states = n_states - 1
    else
      l%s => null()
    end if

    call nullify_list(l)

  end subroutine deallocate_list

  !------------------------------------------------------------------------------!
    subroutine patch(l, s)                                                       !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to append state s to every dangling output in ptr_list l.          !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(ptr_list), pointer, intent(inout)  ::  l                              !
  !     List to be patched                                                       !
  !                                                                              !
  !   type(state),    pointer, intent(in)     ::  s                              !
  !     state with which to patch the list                                       !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    type(ptr_list), pointer, intent(inout)  ::  l
    type(state),    pointer, intent(in)     ::  s

    type(ptr_list), pointer :: tmp_l

    tmp_l => null()

    call point_list(tmp_l, l)
    do while ( associated(tmp_l) )
      select case(tmp_l%side)
        case(1)
          tmp_l%s%out1 => s
        case(2)
          tmp_l%s%out2 => s
        case default
          call throw_error("Unexpected value of side")
      end select
      call point_list(tmp_l, tmp_l%next)
    end do

    call nullify_list(tmp_l)

  end subroutine patch

  !------------------------------------------------------------------------------!
    subroutine allocate_nfa(nfa)                                                 !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to allocate and initialise the constituent parts of the nfa type.  !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(nfa), intent(inout) :: nfa                                            !
  !     Finite automaton to be allocated                                         !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-12-29                                                 !
  !------------------------------------------------------------------------------!
    type(nfa_type), intent(inout) :: nfa

    nfa%head => null()
    nfa%states => null()
    call point_list(nfa%states, new_list(null(), 0))
    nfa%n_states = 0

  end subroutine allocate_nfa

  !------------------------------------------------------------------------------!
    subroutine deallocate_nfa(nfa)                                               !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to deallocate the constituent parts of the nfa type.               !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   type(nfa), intent(inout) :: nfa                                            !
  !     Finite automaton to be allocated                                         !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-12-29                                                 !
  !------------------------------------------------------------------------------!
    type(nfa_type), intent(inout) :: nfa

    call deallocate_list(nfa%states, keep_states=.false., n_states = nfa%n_states)
    nfa%head => null()
    if (nfa%n_states /= 0) call throw_warning("Some states are still allocated!")

  end subroutine deallocate_nfa


end module nfa_mod
