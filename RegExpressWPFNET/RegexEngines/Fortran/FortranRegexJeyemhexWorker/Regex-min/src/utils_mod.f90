module utils_mod
!==============================================================================#
! UTILS_MOD
!------------------------------------------------------------------------------#
! Author:  Ed Higgins <ed.j.higgins@gmail.com>
!------------------------------------------------------------------------------#
! Version: 0.1.1, 2024-09-05
!------------------------------------------------------------------------------#
! This code is distributed under the MIT license.
!==============================================================================#
  implicit none

  private

  public :: throw_warning, throw_error

contains
  !------------------------------------------------------------------------------!
    subroutine throw_error(error, regex, location)                                     !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Throw an error and abort the program                                       !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*), intent(in) :: error                                      !
  !     String to tell the user what's happened                                  !
  !   character(len=*), intent(in), optional :: regex                            !
  !     The regex that has failed                                                !
  !   integer,          intent(in), optional :: location                         !
  !     Where in the regex the failure occured                                   !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2019-08-18                                                 !
  !------------------------------------------------------------------------------!
    use ISO_FORTRAN_ENV, only: error_unit
     character(len=*), intent(in)           :: error
     character(len=*), intent(in), optional :: regex
     integer,          intent(in), optional :: location

     integer :: i

     write(error_unit, *) ""
     write(error_unit, '(2a)') "ERROR: ", error

     if (present(regex)) then
       write(error_unit, '(a)') "Problem occured in regular expression:"
       write(error_unit, '(a)') '  /' // regex //'/'

       if (present(location)) then
         do i=1, location+2
           write(error_unit, '(a)', advance="no") " "
         end do
         write(error_unit, '(a)') "^ Here"
       end if
     end if

     error stop

   end subroutine throw_error

  !------------------------------------------------------------------------------!
    subroutine throw_warning(error, regex, location)                                      !
  !------------------------------------------------------------------------------!
  ! DESCRPTION                                                                   !
  !   Throw a warning but don't abort the program                                !
  !------------------------------------------------------------------------------!
  ! ARGUMENTS                                                                    !
  !   character(len=*), intent(in) :: error                                      !
  !     String to tell the user what's happened                                  !
  !   character(len=*), intent(in), optional :: regex                            !
  !     The regex that has failed                                                !
  !   integer                     , optional :: location                         !
  !     Where in the regex the failure occured                                   !
  !------------------------------------------------------------------------------!
  ! AUTHORS                                                                      !
  !   Edward Higgins, 2019-08-18                                                 !
  !------------------------------------------------------------------------------!
    use ISO_FORTRAN_ENV, only: error_unit
     character(len=*), intent(in)           :: error
     character(len=*), intent(in), optional :: regex
     integer,          intent(in), optional :: location

     integer :: i

     write(error_unit, '(2a)') "WARNING: ", error

     if (present(regex)) then
       write(error_unit, '(a)') "Problem occured in regular expression:"
       write(error_unit, '(a)') '  /' // regex //'/'

       if (present(location)) then
         do i=1, location+2
           write(error_unit, '(a)', advance="no") " "
         end do
         write(error_unit, '(a)') "^ Here"
       end if
     end if

   end subroutine throw_warning

  

end module utils_mod
