#include "pch.h"

#include "SEHFilter.h"


unsigned long SEHFilter( unsigned long code, char* errorText, size_t errorTextSize )
{
    const char* text;

    switch( code )
    {

#define E(e) case e: text = #e; break;

        E( EXCEPTION_ACCESS_VIOLATION )
            E( EXCEPTION_DATATYPE_MISALIGNMENT )
            E( EXCEPTION_BREAKPOINT )
            E( EXCEPTION_SINGLE_STEP )
            E( EXCEPTION_ARRAY_BOUNDS_EXCEEDED )
            E( EXCEPTION_FLT_DENORMAL_OPERAND )
            E( EXCEPTION_FLT_DIVIDE_BY_ZERO )
            E( EXCEPTION_FLT_INEXACT_RESULT )
            E( EXCEPTION_FLT_INVALID_OPERATION )
            E( EXCEPTION_FLT_OVERFLOW )
            E( EXCEPTION_FLT_STACK_CHECK )
            E( EXCEPTION_FLT_UNDERFLOW )
            E( EXCEPTION_INT_DIVIDE_BY_ZERO )
            E( EXCEPTION_INT_OVERFLOW )
            E( EXCEPTION_PRIV_INSTRUCTION )
            E( EXCEPTION_IN_PAGE_ERROR )
            E( EXCEPTION_ILLEGAL_INSTRUCTION )
            E( EXCEPTION_NONCONTINUABLE_EXCEPTION )
            E( EXCEPTION_STACK_OVERFLOW )
            E( EXCEPTION_INVALID_DISPOSITION )
            E( EXCEPTION_GUARD_PAGE )
            E( EXCEPTION_INVALID_HANDLE )
            //?E( EXCEPTION_POSSIBLE_DEADLOCK         )

#undef E

    default:
        return EXCEPTION_CONTINUE_SEARCH; // also covers code E06D7363, probably associated with 'throw std::exception'
    }

    StringCchCopyA( errorText, errorTextSize, "SEH Error: " );
    StringCchCatA( errorText, errorTextSize, text );

    return EXCEPTION_EXECUTE_HANDLER;
}
