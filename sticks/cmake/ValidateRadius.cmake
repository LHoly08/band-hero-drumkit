# Validate before inserting a cache string into a C++ compile definition.
function(bh_validate_radius value output)
    if(NOT "${value}" MATCHES "^([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$")
        message(FATAL_ERROR "RADIUS must be a positive finite number in meters, without spaces or a unit suffix")
    endif()
    # Bounds of positive IEEE-754 binary32 values used by SphereConfig.
    if(value LESS 1.401298464324817e-45 OR value GREATER 3.4028234663852886e38)
        message(FATAL_ERROR "RADIUS must be positive and within the finite float range")
    endif()
    if(NOT "${value}" MATCHES "[.eE]")
        # Force a decimal floating literal, including values with leading zeros.
        set(value "${value}.0")
    endif()
    set(${output} "${value}" PARENT_SCOPE)
endfunction()
