#pragma once

// Unreal defines "check" as a macro.
// ROS2/rclcpp has functions named check(), so we must hide Unreal's macro
// while including rclcpp headers.

#ifdef check
    #pragma push_macro("check")
    #undef check
    #define TEMPO_ROS_RESTORE_UE_CHECK_MACRO
#endif

#include "rclcpp/rclcpp.hpp"

#ifdef TEMPO_ROS_RESTORE_UE_CHECK_MACRO
    #pragma pop_macro("check")
    #undef TEMPO_ROS_RESTORE_UE_CHECK_MACRO
#endif
