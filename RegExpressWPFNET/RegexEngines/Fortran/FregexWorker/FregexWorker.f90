!  FregexWorker.f90 
!
!  FUNCTIONS:
!  FregexWorker - Entry point of console application.
!

!****************************************************************************
!
!  PROGRAM: FregexWorker
!
!  PURPOSE:  Entry point for the console application.
!
!****************************************************************************

    program FregexWorker

    use, intrinsic :: iso_fortran_env, &
        only: int64, stdin => input_unit, &
        stdout => output_unit, &
        stderr => error_unit
    
    implicit none

    block
   
        use fregex

        character (:), allocatable :: pattern, text, flags_s, step_limit_s, result
        character (256) err_msg

        integer :: flags = 0
        integer(int64) :: step_limit
        type(regex_t) :: re
        type(match_t) :: match
        type(match_t), allocatable :: matches(:)
        integer :: stat
        character(:), allocatable :: errmsg
        integer :: i, k

        call read_line( stdin, pattern)
        call read_line( stdin, text)
        call read_line( stdin, flags_s)
        call read_line( stdin, step_limit_s)

        pattern = replace_all( pattern, CHAR(27) // "r", CHAR(13))
        pattern = replace_all( pattern, CHAR(27) // "n", CHAR(10))

        text = replace_all( text, CHAR(27) // "r", CHAR(13))
        text = replace_all( text, CHAR(27) // "n", CHAR(10))

        !print *, pattern
        !print *, text
        !print *, flags_s
        !print *, step_limit_s
        
        if( index( flags_s, "i") > 0) flags = IOR( flags, RE_IGNORECASE)
        if( index( flags_s, "m") > 0) flags = IOR( flags, RE_MULTILINE)
        if( index( flags_s, "s") > 0) flags = IOR( flags, RE_DOTALL)
        
        step_limit_s = TRIM( step_limit_s)

        re = regex( pattern, flags, stat, errmsg)
        
        if( len( step_limit_s) > 0) then
            read( step_limit_s, *, iostat = stat) step_limit
            if( stat == 0) call re%set_step_limit( step_limit)
        end if
        
        if( stat /= RE_OK) then 
            write( stderr, "('Error #', i0, ': ', at)") stat, errmsg        
            stop
        end if 
        
        block
            ! possible names
        
            type(regex_t) :: re_names
            character (:), allocatable :: possible_name
            integer :: group_index
        
            re_names = regex( "\(\?P?<(.*?)>")
            matches = re_names%find_all( pattern)
            do i = 1, size( matches)
                match = matches( i)
                possible_name = match%group( 1)
                group_index = re%group_index( possible_name)
                if( group_index > 0 ) write( stdout, "('n ', i0, ' ', i0, ' ', i0)") group_index, match%start(1), match%end(1)
            end do
	    end block

        ! find matches
        
        matches = re%find_all( text, stat, errmsg)
       
        if( stat /= RE_OK) then 
            write( stderr, "('Error #', i0, ': ', at)") stat, errmsg        
            stop
        end if 
        
        do i = 1, size( matches)
            match = matches( i)

            write( stdout, "('m ', i0, ' ', i0)") match%start(0), match%end(0)
            
            do k = 1, match%group_count()
                write( stdout, "('g ', i0, ' ', i0)") match%start(k), match%end(k)
            end do
        end do
        
    end block
    
    stop
    
    contains
    
    subroutine read_line(unit, line) ! https://stackoverflow.com/questions/31084087/using-a-deferred-length-character-string-to-read-user-input
    integer, intent(in) :: unit
    character(:), intent(out), allocatable :: line

    integer :: stat
    character(512) :: buffer
    integer :: size

    line = ""

    do
        read (unit, "(a)", advance='no', iostat=stat, size=size) buffer

        if (stat > 0) then
            write(stderr, *) "error reading line"
            stop
        end if

        line = line // buffer(:size)

        if (stat < 0) return ! end
    end do

    end subroutine read_line

    function replace_all(str, old, new) result(replaced) ! https://learnxbyexample.com/fortran/string-functions/
    character(len=*), intent(in) :: str, old, new
    character(len=:), allocatable :: replaced
    integer :: i
    replaced = str
    do i = 1, len(str) - len(old) + 1
        if (replaced(i:i+len(old)-1) == old) then
            replaced = replaced(:i-1) // new // replaced(i+len(old):)
        end if
    end do
    end function replace_all

    end program FregexWorker
