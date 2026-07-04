module states_mod
!==============================================================================#
! STATES_MOD
!------------------------------------------------------------------------------#
! Author:  Ed Higgins <ed.j.higgins@gmail.com>
!------------------------------------------------------------------------------#
! Version: 0.1.1, 2024-09-05
!------------------------------------------------------------------------------#
! This code is distributed under the MIT license.
!==============================================================================#
  use utils_mod
  implicit none

  public

  ! Special NFA states
  integer,  parameter ::  null_st      = -1  ! denotes a NULL node in the nfa
  integer,  parameter ::  split_st     = -256 ! denotes a SPLIT node in the nfa
  integer,  parameter ::  match_st     = -257 ! denotes a MATCH node in the nfa

  ! /re/ and postscript operators
  integer,  parameter ::  star_op      = -301 ! * operator (0 or more)
  integer,  parameter ::  plus_op      = -302 ! + operator (1 or more)
  integer,  parameter ::  quest_op     = -303 ! ? operator (0 or 1)
  integer,  parameter ::  or_op        = -304 ! | operator (a or b)
  integer,  parameter ::  cat_op       = -305 ! . operator (cats 2 fragments)
  integer,  parameter ::  open_par_ch  = -306 ! ( operator (for constructing match list)
  integer,  parameter ::  close_par_ch = -307 ! ) operator (for constructing match list)

  ! NFA special matches
  integer,  parameter ::  any_ch       = -401 ! .  match (anything)
  integer,  parameter ::  alpha_ch     = -402 ! \a match ([a..z]|[A..Z])
  integer,  parameter ::  numeric_ch   = -403 ! \d match ([0..9])
  integer,  parameter ::  word_ch      = -404 ! \w match (\d|\a|_)
  integer,  parameter ::  space_ch     = -405 ! \s match (" "|\t)
  integer,  parameter ::  n_alpha_ch   = -406 ! \A match (anything but \a)
  integer,  parameter ::  n_numeric_ch = -407 ! \D match (anything but \d)
  integer,  parameter ::  n_word_ch    = -408 ! \W match (anything but \w)
  integer,  parameter ::  n_space_ch   = -409 ! \S match (anything but \s)
  integer,  parameter ::  start_ch     = -410 ! ^  match (start of the string)
  integer,  parameter ::  finish_ch    = -411 ! $  match (end of the string)

contains
  !------------------------------------------------------------------------------!
    function state_str(ch) result(token)                                         !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Convert an integer char code to a printable token                          !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   integer, intent(in) :: pf(:)                                               !
  !     Postfix expression stored as an array of integers                        !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   char(len=5) :: token                                                       !
  !     The printable state string                                               !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2019-07-19                                                 !
  !------------------------------------------------------------------------------!
    character(len=5)      :: token
    integer,  intent(in)  :: ch

      select case(ch)
        case(null_st)
          token = "     "
        case(1:255)
          token = achar(ch) // "   "
        case(open_par_ch)
          token = "OP ( "
        case(close_par_ch)
          token = "CL ) "
        case(cat_op)
          token = "CAT  "
        case(plus_op)
          token = "PLUS "
        case(or_op)
          token = "OR   "
        case(quest_op)
          token = "QUE  "
        case(star_op)
          token = "STAR "

        case(split_st)
          token = "SPLIT"
        case(match_st)
          token = "MATCH"
        case(any_ch)
          token = ".    "
        case(start_ch)
          token = "START"
        case(finish_ch)
          token = "FIN  "
        case(alpha_ch)
          token = "\a   "
        case(numeric_ch)
          token = "\d   "
        case(word_ch)
          token = "\w   "
        case(space_ch)
          token = "\s   "
        case(n_alpha_ch)
          token = "\A   "
        case(n_numeric_ch)
          token = "\D   "
        case(n_word_ch)
          token = "\W   "
        case(n_space_ch)
          token = "\S   "
        case default
          call throw_error("Unrecognised character" //  char(ch))
      end select

    end function state_str

end module states_mod
