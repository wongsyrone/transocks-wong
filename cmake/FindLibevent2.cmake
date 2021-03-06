include(MacroPushRequiredVars)
MACRO_PUSH_REQUIRED_VARS(CMAKE_FIND_LIBRARY_SUFFIXES)

FIND_PATH(LIBEVENT2_INCLUDE_DIR event2/event.h)

IF(Libevent2_USE_STATIC_LIB)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
ENDIF()

FIND_LIBRARY(LIBEVENT2_LIBRARIES NAMES event_core)

IF(LIBEVENT2_INCLUDE_DIR)
    SET(LIBEVENT2_FOUND_INCLUDE TRUE)
ENDIF(LIBEVENT2_INCLUDE_DIR)
IF(LIBEVENT2_LIBRARIES)
    SET(LIBEVENT2_FOUND_LIBRARY TRUE)
ENDIF(LIBEVENT2_LIBRARIES)
IF(LIBEVENT2_FOUND_INCLUDE AND LIBEVENT2_FOUND_LIBRARY)
    SET(LIBEVENT2_FOUND TRUE)
ENDIF(LIBEVENT2_FOUND_INCLUDE AND LIBEVENT2_FOUND_LIBRARY)

IF(LIBEVENT2_FOUND_INCLUDE)
    IF (NOT Libevent2_FIND_QUIETLY)
        MESSAGE(STATUS "Found libevent2 includes:	${LIBEVENT2_INCLUDE_DIR}/event2/event.h")
    ENDIF (NOT Libevent2_FIND_QUIETLY)
ELSE(LIBEVENT2_FOUND_INCLUDE)
    IF (Libevent2_FIND_REQUIRED)
        MESSAGE(FATAL_ERROR "Could NOT find libevent2 include headers")
    ENDIF (Libevent2_FIND_REQUIRED)
ENDIF(LIBEVENT2_FOUND_INCLUDE)

IF(LIBEVENT2_FOUND_LIBRARY)
    IF (NOT Libevent2_FIND_QUIETLY)
        MESSAGE(STATUS "Found libevent2 library: ${LIBEVENT2_LIBRARIES}")
    ENDIF (NOT Libevent2_FIND_QUIETLY)
ELSE(LIBEVENT2_FOUND_LIBRARY)
    IF (Libevent2_FIND_REQUIRED)
        MESSAGE(FATAL_ERROR "Could NOT find libevent2 library")
    ENDIF (Libevent2_FIND_REQUIRED)
ENDIF(LIBEVENT2_FOUND_LIBRARY)

MACRO_POP_REQUIRED_VARS(CMAKE_FIND_LIBRARY_SUFFIXES)