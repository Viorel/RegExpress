module postfix_mod
!==============================================================================#
! POSTFIX_MOD
!------------------------------------------------------------------------------#
! Author:  Ed Higgins <ed.j.higgins@gmail.com>
!------------------------------------------------------------------------------#
! Version: 0.1.1, 2024-09-05
!------------------------------------------------------------------------------#
! This code is distributed under the MIT license.
!==============================================================================#
  use utils_mod
  use states_mod
  implicit none

  private

  public :: build_postfix, print_postfix

  integer,  parameter, public ::  pf_buff_size    = 8192  ! Maximum size of the postfix buffer
  integer,  parameter, public ::  pf_stack_size   = 4096  ! Maximum size of the postfix stack
  integer,  parameter ::  max_paren_depth = 128   ! Maximum depth of nested ()'s

  ! List of parentheses for building the postfix (I'll be honest, I don't quite get how this works)
  type  ::  paren_list
    integer ::  n_atom
    integer ::  n_alt
  end type paren_list

contains
  !------------------------------------------------------------------------------!
    function build_postfix(re) result(pf)                                         !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to convert a regular expression string to a a postfix expression,  !
  !   stored in an array of integers.                                            !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*),   intent(in) :: re
  !     Regular expression to be converted to postfix                            !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   integer ::  pf(pf_buff_size)                                               !
  !     Postfix expression, stored as an array of integers                       !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    integer ::  pf(pf_buff_size)
    character(len=*),   intent(in) :: re

    character        :: c
    integer          :: n_alt                   ! Number of alternatives
    integer          :: n_atom                  ! Number of single units
    integer          :: re_loc                  ! Location in the regex string
    integer          :: pf_loc                  ! Location in the postfix list
    type(paren_list) :: paren(max_paren_depth)  ! List of opened parens at a given point
    integer          :: par_loc                 ! Current position in the paren list
    integer          :: escaped_chr             ! The charcter which has been escaped
    character(len=16):: mode
    integer          :: comment_bracket_count
    character(len=64):: submatch_name

    ! Initialise key variables
    par_loc = 1
    re_loc  = 1
    pf_loc  = 1
    n_alt   = 0
    n_atom  = 0
    mode = "normal"

    pf = null_st

    ! If the regex won't fit in the pf list, abort
    if (len_trim(re) > pf_buff_size/2) call throw_error("Regex too long", trim(re))

    ! Loop over characters in the regex
    do while (re_loc <= len_trim(re))
      c = re(re_loc:re_loc)
      if (mode == "normal") then

        ! What is the current character?
        select case(c)

          case('\') ! The next character will be escaped
            mode = "escaped"

          case('(') ! We've found an open bracket
            call enter_paren(track=.true.)

          case('|') ! We've found an OR operation
            if (n_atom == 0) call throw_error("OR has no left hand side", re, re_loc)

            ! Add all the current atoms to the postfix list and start a new alternate list
            n_atom = n_atom - 1
            do while (n_atom > 0)
              call push_atom(cat_op)
            end do
            n_alt = n_alt + 1

          case (')') ! We've found a close bracket
            if (par_loc == 1) call throw_error("Unmatched ')'", re, re_loc)
            if (n_atom == 0)  call throw_error("Empty parentheses", re, re_loc)

            call exit_paren(track=.true.)

          case('*') ! We've found a STAR operation
            if (n_atom == 0) call throw_error("Nothing to *", re, re_loc)
            call push_atom(star_op)

          case('+') ! We've found a PLUS operation
            if (n_atom == 0) call throw_error("Nothing to +", re, re_loc)
            call push_atom(plus_op)

          case('?') ! We've found a QUESTION operation
            if (n_atom == 0) call throw_error("Nothing to ?", re, re_loc)
            call push_atom(quest_op)

          case ('.') ! We've found and ANY character
            if (n_atom > 1) call push_atom(cat_op)
            call push_atom(any_ch)

          case ('^') ! We've found a line-start anchor
            if (n_atom > 1) call push_atom(cat_op)
            call push_atom(start_ch)

          case ('$') ! We've foudn a line-end anchor
            if (n_atom > 1) call push_atom(cat_op)
            call push_atom(finish_ch)

          case(' ', achar(9)) ! We've found whitespace in the regex
            ! Do nothing, ignore whitespace in the regex

          case('!') ! We've found a comment
            comment_bracket_count = 0
            mode = "comment"
            ! Do nothing, ignore whitespace in the regex

          case ('[') ! We're entering a character group
            mode = "group"
            call enter_paren(track=.false.)

          case ('<')
            mode = "submatch-name"
            submatch_name = ""

          case default ! We've found a regular charcter
            ! If there are already atoms, add a concat. operation and then add this character
            if (n_atom > 1) call push_atom(cat_op)
            call push_atom(iachar(c))

        end select
      else if (mode == "escaped") then

        ! Deal with escaped characters
        select case(c)
          case('(','|',')','[',']','*','+','?','\','.','^','$','!',' ',achar(9),achar(10))
            escaped_chr = iachar(c)
          case('a')
            escaped_chr = alpha_ch
          case('d')
            escaped_chr = numeric_ch
          case('w')
            escaped_chr = word_ch
          case('s')
            escaped_chr = space_ch
          case('A')
            escaped_chr = n_alpha_ch
          case('D')
            escaped_chr = n_numeric_ch
          case('W')
            escaped_chr = n_word_ch
          case('S')
            escaped_chr = n_space_ch

          case default
            call throw_error("Unrecognised escape character \" // c, re, re_loc)
        end select

        ! If there are already atoms, add a concat. operation and then add this character
        if (n_atom > 1) call push_atom(cat_op)
        call push_atom(escaped_chr)
        mode = "normal"

      ! Handle character groups (e.g. "[aeiou]")
      else if (mode == "group") then
        if (c /= ']') then
          call push_atom(iachar(c))
          n_alt=n_alt+1
          n_atom = n_atom - 1

        else
          n_alt=n_alt-1
          call exit_paren(track = .false.)
          mode = "normal"
        end if

      else if (mode == "comment") then
        if (c == '\') then
          mode = "comment-escaped"
        else if (c == '[') then
          comment_bracket_count = comment_bracket_count + 1
        else if (c == ']') then
          comment_bracket_count = comment_bracket_count - 1
          if (comment_bracket_count == 0) mode = "normal"
        end if

      else if (mode == "comment-escaped") then
        mode = "comment"

      else if (mode == "submatch-name") then
        if  ((c >= '0' .and. c <= '9') &
        .or. (c >= 'a' .and. c <= 'z') &
        .or. (c >= 'a' .and. c <= 'Z') &
        .or. c == '-' .or. c == '_') then
          submatch_name = trim(submatch_name) // c
        else if (c == ':') then
          mode = 'submatch-body'
        else if (c == '>') then
          mode = "normal"
        end if

      else if (mode == "submatch-body") then
        if (c == '>') then
          mode = "normal"
        end if
      end if

      ! Go to the next character in the regex
      re_loc = re_loc + 1
    end do

    if (par_loc /= 1) call throw_error("I think you've got unmatched parentheses", re, re_loc)

    ! Add any remaining atoms to the postfix list
    n_atom = n_atom - 1
    do while (n_atom > 0)
      call push_atom(cat_op)
    end do

    ! Add any remaining alternatives to the postfix list
    do while (n_alt > 0)
      call push_atom(or_op)
    end do

  contains

    ! A routine to push a given atom to the end of the postfix list
    subroutine push_atom(atom)
      integer, intent(in) :: atom
      integer :: tmp_pf_loc

      pf(pf_loc) = atom
      pf_loc = pf_loc + 1

      select case(atom)
        case (cat_op)
          n_atom = n_atom - 1

        case (or_op)
          n_alt = n_alt - 1

        ! If this character operates on a previous atom, we need to make sure
        ! that it applied to that atom and not any close_par.s instead
        case (quest_op, plus_op, star_op)
          tmp_pf_loc = pf_loc-1
          do while (pf(tmp_pf_loc-1) == close_par_ch)
            pf(tmp_pf_loc) = close_par_ch
            pf(tmp_pf_loc-1) = atom
            tmp_pf_loc = tmp_pf_loc - 1
          end do

        case default
        n_atom = n_atom + 1

      end select

    end subroutine push_atom

    subroutine enter_paren(track)
      logical :: track

      if (par_loc > size(paren)) call throw_error("Too many embedded brackets!", re, re_loc)

      ! Concatinate this set of brackets with anything previous and add it to the postfix list
      if (n_atom > 1) call push_atom(cat_op)
      ! Push an open_paren to track brackets in the automaton
      if (track) call push_atom(open_par_ch)

      ! Store the state outside of the brackes and reset the counters
      paren(par_loc)%n_alt  = n_alt
      paren(par_loc)%n_atom = n_atom
      par_loc = par_loc + 1
      n_alt   = 0
      n_atom  = 0

    end subroutine enter_paren

    subroutine exit_paren(track)
      logical :: track

      ! Add all the current atoms to the postfix list
      n_atom = n_atom - 1
      do while (n_atom > 0)
        call push_atom(cat_op)
      end do

      ! Close off any alternatives that exist in the bracktes
      do while (n_alt > 0)
        call push_atom(or_op)
      end do

      ! Revert state to that of the outer brackets
      par_loc = par_loc - 1
      n_alt = paren(par_loc)%n_alt
      n_atom = paren(par_loc)%n_atom
      n_atom = n_atom + 1

      ! Push a close_paren to track brackets in the automaton
      if (track) call push_atom(close_par_ch)

    end subroutine exit_paren

  end function build_postfix

  !------------------------------------------------------------------------------!
    subroutine print_postfix(pf)                                                      !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to print out a postfix expression in a human readable manner       !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   integer, intent(in) :: pf(:)                                               !
  !     Postfix expression stored as an array of integers                        !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    integer,  intent(in)  ::  pf(:)

    integer ::  i

    print_loop: do i = 1, size(pf)
      if (pf(i) == null_st) exit print_loop
      write(*,'(A7,A5)') state_str(pf(i))
    end do print_loop

  end subroutine print_postfix

end module postfix_mod
