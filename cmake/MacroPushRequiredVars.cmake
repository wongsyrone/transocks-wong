# this module defines two macros:
# MACRO_PUSH_REQUIRED_VARS()
# and
# MACRO_POP_REQUIRED_VARS()
# use these if you call cmake macros which use
# any of the CMAKE_REQUIRED_XXX variables
#
# Usage:
# MACRO_PUSH_REQUIRED_VARS()
# SET(CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS} -DSOME_MORE_DEF)
# CHECK_FUNCTION_EXISTS(...)
# MACRO_POP_REQUIRED_VARS()

# Copyright (c) 2006, Alexander Neundorf, <neundorf@kde.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

MACRO(MACRO_PUSH_REQUIRED_VARS)

    IF(NOT DEFINED _PUSH_REQUIRED_VARS_COUNTER)
        SET(_PUSH_REQUIRED_VARS_COUNTER 0)
    ENDIF(NOT DEFINED _PUSH_REQUIRED_VARS_COUNTER)

    MATH(EXPR _PUSH_REQUIRED_VARS_COUNTER "${_PUSH_REQUIRED_VARS_COUNTER}+1")

    SET(_CMAKE_REQUIRED_INCLUDES_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}    ${CMAKE_REQUIRED_INCLUDES})
    SET(_CMAKE_REQUIRED_DEFINITIONS_SAVE_${_PUSH_REQUIRED_VARS_COUNTER} ${CMAKE_REQUIRED_DEFINITIONS})
    SET(_CMAKE_REQUIRED_LIBRARIES_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}   ${CMAKE_REQUIRED_LIBRARIES})
    SET(_CMAKE_REQUIRED_FLAGS_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}       ${CMAKE_REQUIRED_FLAGS})
ENDMACRO(MACRO_PUSH_REQUIRED_VARS)

MACRO(MACRO_POP_REQUIRED_VARS)

    # don't pop more than we pushed
    IF("${_PUSH_REQUIRED_VARS_COUNTER}" GREATER "0")

        SET(CMAKE_REQUIRED_INCLUDES    ${_CMAKE_REQUIRED_INCLUDES_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}})
        SET(CMAKE_REQUIRED_DEFINITIONS ${_CMAKE_REQUIRED_DEFINITIONS_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}})
        SET(CMAKE_REQUIRED_LIBRARIES   ${_CMAKE_REQUIRED_LIBRARIES_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}})
        SET(CMAKE_REQUIRED_FLAGS       ${_CMAKE_REQUIRED_FLAGS_SAVE_${_PUSH_REQUIRED_VARS_COUNTER}})

        MATH(EXPR _PUSH_REQUIRED_VARS_COUNTER "${_PUSH_REQUIRED_VARS_COUNTER}-1")
    ENDIF("${_PUSH_REQUIRED_VARS_COUNTER}" GREATER "0")

ENDMACRO(MACRO_POP_REQUIRED_VARS)