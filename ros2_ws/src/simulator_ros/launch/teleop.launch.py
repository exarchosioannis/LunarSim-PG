from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    default_config_file = PathJoinSubstitution([
        FindPackageShare('simulator_ros'),
        'config',
        'f710.yaml'
    ])

    config_file = LaunchConfiguration('config_file')
    joy_device_id = LaunchConfiguration('joy_device_id')
    joy_device_name = LaunchConfiguration('joy_device_name')
    joy_deadzone = LaunchConfiguration('joy_deadzone')
    joy_autorepeat_rate = LaunchConfiguration('joy_autorepeat_rate')
    joy_topic = LaunchConfiguration('joy_topic')
    cmd_vel_topic = LaunchConfiguration('cmd_vel_topic')

    return LaunchDescription([
        DeclareLaunchArgument(
            'config_file',
            default_value=default_config_file,
            description='teleop_twist_joy parameter file'
        ),
        DeclareLaunchArgument(
            'joy_device_id',
            default_value='0',
            description='Joystick device index used when joy_device_name is empty'
        ),
        DeclareLaunchArgument(
            'joy_device_name',
            default_value='Logitech Gamepad F710',
            description='SDL joystick name; set to an empty string to select by device ID'
        ),
        DeclareLaunchArgument(
            'joy_deadzone',
            default_value='0.05',
            description='Joystick axis dead zone'
        ),
        DeclareLaunchArgument(
            'joy_autorepeat_rate',
            default_value='20.0',
            description='Rate in Hz at which held joystick values are republished'
        ),
        DeclareLaunchArgument(
            'joy_topic',
            default_value='/joy',
            description='Joystick message topic'
        ),
        DeclareLaunchArgument(
            'cmd_vel_topic',
            default_value='/cmd_vel',
            description='Unstamped geometry_msgs/msg/Twist output topic'
        ),
        Node(
            package='joy',
            executable='joy_node',
            name='joy_node',
            output='screen',
            parameters=[{
                'device_id': ParameterValue(joy_device_id, value_type=int),
                'device_name': ParameterValue(joy_device_name, value_type=str),
                'deadzone': ParameterValue(joy_deadzone, value_type=float),
                'autorepeat_rate': ParameterValue(
                    joy_autorepeat_rate,
                    value_type=float
                ),
            }],
            remappings=[('joy', joy_topic)]
        ),
        Node(
            package='teleop_twist_joy',
            executable='teleop_node',
            name='teleop_twist_joy',
            output='screen',
            parameters=[config_file],
            remappings=[
                ('joy', joy_topic),
                ('cmd_vel', cmd_vel_topic),
            ],
        ),
    ])
