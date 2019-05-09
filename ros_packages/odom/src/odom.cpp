#include "ros/ros.h"
#include <geometry_msgs/Vector3.h>
#include <tf/transform_broadcaster.h>
#include <nav_msgs/Odometry.h>

#define wheel_distance 20
#define wheel_diameter 8.4

long _PreviousLeftEncoderCounts = 0;
long _PreviousRightEncoderCounts = 0;
double DistancePerCount = (3.14159265 * 8.4) / 4000;

double x;
double y;
double th;

double vx;
double vy;
double vth;
double deltaLeft;
double deltaRight;

ros::Time current_time_encoder, last_time_encoder;

void WheelCallback(const geometry_msgs::Vector3::ConstPtr& ticks)
{
  current_time_encoder = ros::Time::now();

  deltaLeft = ticks->x - _PreviousLeftEncoderCounts;
  deltaRight = ticks->y - _PreviousRightEncoderCounts;

  vx = (deltaLeft + deltaRight) / 2 * DistancePerCount;
  vy = 0; // we have a non-holonomic constraint (for a holonomic robot, this is non-zero)
  vth = (deltaRight - deltaLeft) / wheel_distance;

  _PreviousLeftEncoderCounts = ticks->x;
  _PreviousRightEncoderCounts = ticks->y;
  last_time_encoder = current_time_encoder;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "odometry_publisher");
  ros::NodeHandle n;
  ros::Subscriber sub = n.subscribe("encoder_ticks", 100, WheelCallback);
  ros::Publisher odom_pub = n.advertise<nav_msgs::Odometry>("odom", 50);
  tf::TransformBroadcaster odom_broadcaster;

  ros::Time current_time, last_time;
  current_time = ros::Time::now();
  last_time = ros::Time::now();

  ros::Rate r(10.0);
  while(n.ok()){

    current_time = ros::Time::now();

    //compute odometry in a typical way given the velocities of the robot
    double dt = (current_time - last_time).toSec();
    double delta_x = (vx * cos(th) - vy * sin(th)) * dt;
    double delta_y = (vx * sin(th) + vy * cos(th)) * dt;
    double delta_th = vth * dt;

    x += delta_x;
    y += delta_y;
    th += delta_th;

    //since all odometry is 6DOF we'll need a quaternion created from yaw
    geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(th);

    //first, we'll publish the transform over tf
    geometry_msgs::TransformStamped odom_trans;
    odom_trans.header.stamp = current_time;
    odom_trans.header.frame_id = "odom";
    odom_trans.child_frame_id = "base_link";

    odom_trans.transform.translation.x = x;
    odom_trans.transform.translation.y = y;
    odom_trans.transform.translation.z = 0.0;
    odom_trans.transform.rotation = odom_quat;

    //send the transform
    odom_broadcaster.sendTransform(odom_trans);

    //next, we'll publish the odometry message over ROS
    nav_msgs::Odometry odom;
    odom.header.stamp = current_time;
    odom.header.frame_id = "odom";

    //set the position
    odom.pose.pose.position.x = x;
    odom.pose.pose.position.y = y;
    odom.pose.pose.position.z = 0.0;
    odom.pose.pose.orientation = odom_quat;

    //set the velocity
    odom.child_frame_id = "base_link";
    odom.twist.twist.linear.x = vx;
    odom.twist.twist.linear.y = vy;
    odom.twist.twist.angular.z = vth;

    //publish the message
    odom_pub.publish(odom);

    last_time = current_time;

    ros::spinOnce();
    r.sleep();
  }
}
