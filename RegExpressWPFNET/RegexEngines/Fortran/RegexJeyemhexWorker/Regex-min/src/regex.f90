!==============================================================================!
! REGEX                                                                        !
!==============================================================================!
! Module containing routines for string manipulations with regular             !
! expressions.                                                                 !
!                                                                              !
!   Implementation based on the description (and some of the code) from        !
!   https://swtch.com/~rsc/regexp. Many thanks to Russ for his excellent       !
!   webpage!                                                                   !
!                                                                              !
!------------------------------------------------------------------------------!
! Author:  Edward Higgins <ed.j.higgins@gmail.com>                             !
!------------------------------------------------------------------------------!
! Version: 0.3.1, 2017-12-04                                                   !
!------------------------------------------------------------------------------!
! This code is distributed under the MIT license.                              !
!==============================================================================!

module regex
  use utils_mod
  use states_mod
  use postfix_mod
  use nfa_mod
  implicit none

  private

  public :: re_match, re_match_str, re_split, re_replace


  logical,  parameter :: debug = .false.

contains

  !------------------------------------------------------------------------------!
    function re_match(re, str)                                                   !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to check a string str against a regular expression re.             !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*), intent(in)  ::  re                                       !
  !     Regualr expression to be matched                                         !
  !                                                                              !
  !   character(len=*), intent(in)  ::  str
  !     String to be searched                                                    !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   TRUE if there is a match, FALSE otherwise.                                 !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    logical :: re_match
    character(len=*), intent(in)  ::  re
    character(len=*), intent(in)  ::  str

    integer                 ::  postfix(pf_buff_size)
    type(nfa_type)          ::  nfa
    integer ::  istart

    istart = 1

    if (len_trim(re) < 1) call throw_error("Regular expression cannot be of length 0")
    postfix = build_postfix(trim(re))
    if(debug) call print_postfix(postfix)

    nfa = build_nfa(postfix)
    if(debug) call print_nfa_state(nfa%head)

    re_match = run_nfa_full(nfa, trim(str), istart)

    call deallocate_nfa(nfa)

  end function re_match

  !------------------------------------------------------------------------------!
    function re_match_str(re, str)                                               !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to get a substring from str that matches the regular expression re.!
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*), intent(in)  ::  re                                       !
  !     Regualr expression to be matched                                         !
  !                                                                              !
  !   character(len=*), intent(in)  ::  str                                      !
  !     String to be searched                                                    !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   The matching string if there is a match, an empty string otherwise.        !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    character(len=pf_buff_size) :: re_match_str
    character(len=*), intent(in)  ::  re
    character(len=*), intent(in)  ::  str

    integer                 ::  postfix(pf_buff_size)
    type(nfa_type)          ::  nfa
    integer ::  istart, ifin
    logical :: match

    istart = 1
    ifin = -1

    re_match_str = " "

    if (len_trim(re) < 1) call throw_error("Regular expression cannot be of length 0")
    postfix = build_postfix(trim(re))
    nfa = build_nfa(postfix)

    match = run_nfa_full(nfa, trim(str), istart, finish=ifin)
    if (match) re_match_str = str(istart:ifin)

    call deallocate_nfa(nfa)

  end function re_match_str

  !------------------------------------------------------------------------------!
    subroutine re_split(re, str, output)                                         !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to split a string into an array of substrings, based on the regular!
  !   expression re.                                                             !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*), intent(in)  ::  re                                       !
  !     Regualr expression to be matched                                         !
  !                                                                              !
  !   character(len=*), intent(in)  ::  str                                      !
  !     String to be searched                                                    !
  !                                                                              !
  !   character(len=*), intent(inout), allocatable   :: output(:)                !
  !     Array containing the substrings. This will be (re)allocated within this  !
  !     routine to the size of the number of matches.                            !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    character(len=*), intent(in)  ::  re
    character(len=*), intent(in)  ::  str
    character(len=*), intent(inout), allocatable   :: output(:)

    type(nfa_type)          ::  nfa
    integer                 ::  postfix(pf_buff_size)
    logical                 ::   is_match

    integer :: istart, fin, isplit, last_fin, n_splits

    istart = 1

    if (len_trim(re) < 1) call throw_error("Regular expression cannot be of length 0")
    postfix = build_postfix(trim(re))
    nfa = build_nfa(postfix)

    istart = 1
    isplit = 1
    n_splits = 0

    is_match = run_nfa_full(nfa, trim(str), istart, finish=fin)
    if (is_match) then
      n_splits = n_splits + 1
      last_fin = fin
      istart = last_fin+1
      isplit = 2
      do while (istart <= len_trim(str))
        is_match = run_nfa_full(nfa, trim(str), istart, finish=fin)
        if (.not. is_match) exit
        n_splits = n_splits + 1
        last_fin = fin
        isplit = isplit + 1
        istart = last_fin+1
      end do
      if (last_fin <= len_trim(str)) n_splits = n_splits + 1
    end if

    if (n_splits == 0) return

    if (allocated(output)) deallocate(output)
    allocate(output(n_splits))

    istart = 1
    isplit = 1
    output = " "

    is_match = run_nfa_fast(nfa, trim(str), istart, finish=fin)
    if (is_match) then
      output(1) = str(1:istart-1)
      last_fin = fin
      istart = last_fin+1
      isplit = 2
      do while (istart <= len_trim(str))
        is_match = run_nfa_fast(nfa, trim(str), istart, finish=fin)
        if (.not. is_match) exit
        output(isplit) = str(last_fin+1:istart-1)
        last_fin = fin
        isplit = isplit + 1
        istart = last_fin+1
      end do
      if (last_fin < len_trim(str)) output(isplit) = str(last_fin+1:)
    end if

    call deallocate_nfa(nfa)

  end subroutine re_split

  !------------------------------------------------------------------------------!
    function re_replace(re, repl, str)                                           !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Routine to replace each occurance of re with repl in str                  .!
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*), intent(in)  ::  re                                       !
  !     Regualr expression to be matched                                         !
  !                                                                              !
  !   character(len=*), intent(in)  ::  repl                                     !
  !     String to replace the regular expression                                 !
  !                                                                              !
  !   character(len=*), intent(in)  ::  str                                      !
  !     String to be searched                                                    !
  !------------------------------------------------------------------------------!
  ! RETURNS                                                                      !
  !   The matching string if there is a match, an empty string otherwise.        !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2016-02-01                                                 !
  !------------------------------------------------------------------------------!
    character(len=pf_buff_size) :: re_replace
    character(len=*), intent(in)  ::  re
    character(len=*), intent(in)  ::  repl
    character(len=*), intent(in)  ::  str

    integer                 ::  postfix(pf_buff_size)
    type(nfa_type)          ::  nfa
    integer ::  istart, ifin, last_fin, rep_ptr
    logical :: match

    istart = 1
    ifin = -1
    last_fin = 0
    rep_ptr = 0

    re_replace = " "

    if (len_trim(re) < 1) call throw_error("Regular expression cannot be of length 0")
    postfix = build_postfix(trim(re))
    nfa = build_nfa(postfix)

    match = run_nfa_fast(nfa, trim(str), istart, finish=ifin)
    if (match) then
      re_replace = str(1:istart-1) // repl
      rep_ptr = istart + len(repl)-1
      last_fin = ifin
    end if

    do while (ifin <= len(str))
      istart=ifin+1
      match = run_nfa_fast(nfa, trim(str), istart, finish=ifin)
      if (match) then
        re_replace = re_replace(1:rep_ptr) // str(last_fin+1:istart-1) // repl
        rep_ptr = rep_ptr + (istart-last_fin+1) + len(repl)-2
        last_fin = ifin
      else
        exit
      end if
    end do

    re_replace = re_replace(1:rep_ptr) // str(last_fin+1:)

    call deallocate_nfa(nfa)

  end function re_replace

end module regex
