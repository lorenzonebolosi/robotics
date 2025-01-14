#define _USE_MATH_DEFINES
 
#include <cmath>
#include <iostream>
#include "first_project/Subscriber.h"
#include "first_project/RPM.h"

#include "ros/ros.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/TwistStamped.h"
#include "sensor_msgs/JointState.h"
#include "nav_msgs/Odometry.h"
#include <tf2/LinearMath/Quaternion.h>
#include "tf2/LinearMath/Matrix3x3.h"

Subscriber::Subscriber() {

  sub_wheel = n.subscribe("wheel_states", 1000, &Subscriber::wheelCallback, this);

  velocity_publisher = n.advertise<geometry_msgs::TwistStamped>("cmd_vel", 1000);
  odometry_publisher = n.advertise<nav_msgs::Odometry>("odom", 1000);
  tick_vel_publisher = n.advertise<first_project::RPM>("ticks_to_RPM",100);

  service = n.advertiseService("setPos", &Subscriber::setServicePosition, this);
}

void Subscriber::main_loop() {

  n.getParam("/r", this->r);
  n.getParam("/l", this->l);
  n.getParam("/w", this->w);
  n.getParam("/N", this->N);
  n.getParam("/T", this->T);
  n.getParam("/initialApproximation", approximationType);

  ros::Rate loop_rate(10);

  while (ros::ok()) {

    ros::spinOnce();
    loop_rate.sleep();

  }
}

//subscriber of /wheel_states topic
void Subscriber::wheelCallback(const sensor_msgs::JointState::ConstPtr& msg) {
    
    //from ticks TO robot velocity
    float vx;//robot velocity along x
    float vy;//robot velocity along y
    float W;//robot angular velocity

    //the first iteration only saves initial time and ticks of the bag.
    if(start) {

        float Ts;

        Ts = (msg->header.stamp - this->old_time).toSec(); // delta time

        //ticks to RPM (used to check with RPM computed in velocity.cpp)
        first_project::RPM ticks_RPM;
        ticks_RPM.header.stamp = msg->header.stamp;
        ticks_RPM.rpm_fl = ((msg->position[0] - this->old_ticks[0]) * 2 * M_PI) / (N * T * Ts);
        ticks_RPM.rpm_fr = ((msg->position[1] - this->old_ticks[1]) * 2 * M_PI) / (N * T * Ts);
        ticks_RPM.rpm_rl = ((msg->position[2] - this->old_ticks[2]) * 2 * M_PI) / (N * T * Ts);
        ticks_RPM.rpm_rr = ((msg->position[3] - this->old_ticks[3]) * 2 * M_PI) / (N * T * Ts);

        this->tick_vel_publisher.publish(ticks_RPM);

        //from RPM to robot velocity
        vx = (ticks_RPM.rpm_fl + ticks_RPM.rpm_fr + ticks_RPM.rpm_rl + ticks_RPM.rpm_rr) * r / 4;
        vy = (-ticks_RPM.rpm_fl + ticks_RPM.rpm_fr + ticks_RPM.rpm_rl - ticks_RPM.rpm_rr) * r / 4;
        W = (-ticks_RPM.rpm_fl + ticks_RPM.rpm_fr - ticks_RPM.rpm_rl + ticks_RPM.rpm_rr) * r / (4 * (l + w));


        velocityPublisher(vx, vy, W, msg->header.stamp);

        //integration of vx, vy and W to find the pose (x,y,theta)

        float x, y, theta;
        if (this->approximationType == 0) {
            //Euler method
            x = this->x_old + vx * Ts * cos(this->theta_old) - vy * Ts * sin(this->theta_old);
            y = this->y_old + vx * Ts * sin(this->theta_old) + vy * Ts * cos(this->theta_old);
        } else {
            //Runge Kutta
            x = this->x_old + vx * Ts * cos(this->theta_old + (W * Ts / 2)) - vy * Ts * sin(this->theta_old + (W * Ts / 2));
            y = this->y_old + vx * Ts * sin(this->theta_old + (W * Ts / 2)) + vy * Ts * cos(this->theta_old + (W * Ts / 2));
        }
        theta = this->theta_old + W * Ts;


        odometryPublisher(x, vx, y, vy, theta, W, msg->header.stamp);
        odometryBroadcast(x, y, theta, msg->header.stamp);

        this->x_old = x;
        this->y_old = y;
        this->theta_old = theta;
    }
    start = true;

    //save x, y, theta and time for next integration
    this->old_time=msg->header.stamp;
    //save old ticks
    for (int i = 0; i < msg->position.size(); i++) {
        this->old_ticks[i] = msg->position[i];
    };

    ros::spinOnce();
}

//publish robot velocity on /cmd_vel topic
void Subscriber::velocityPublisher(float vx, float vy, float W, ros::Time stamp){

    geometry_msgs::TwistStamped velocity_msg;

    velocity_msg.header.stamp = stamp;

    velocity_msg.twist.linear.x = vx;
    velocity_msg.twist.linear.y = vy;
    velocity_msg.twist.linear.z = 0;

    velocity_msg.twist.angular.x = 0;
    velocity_msg.twist.angular.y = 0;
    velocity_msg.twist.angular.z = W;

    this->velocity_publisher.publish(velocity_msg);
}

//publish robot odometry on /odom topic
void Subscriber::odometryPublisher(float x, float vx, float y, float vy, float theta, float W, ros::Time stamp){

    nav_msgs::Odometry odometry_msg;

    odometry_msg.header.stamp = stamp;
    odometry_msg.header.frame_id = "odom";
    odometry_msg.child_frame_id = "base_link";

    //set the position
    odometry_msg.pose.pose.position.x = x;
    odometry_msg.pose.pose.position.y = y;
    odometry_msg.pose.pose.position.z = 0.0;

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, theta);
    odometry_msg.pose.pose.orientation.x = q.x();
    odometry_msg.pose.pose.orientation.y = q.y();
    odometry_msg.pose.pose.orientation.z = q.z();
    odometry_msg.pose.pose.orientation.w = q.w();

    //set the velocity
    odometry_msg.twist.twist.linear.x = vx;
    odometry_msg.twist.twist.linear.y = vy;
    odometry_msg.twist.twist.linear.z = 0.0;

    odometry_msg.twist.twist.angular.x = 0.0;
    odometry_msg.twist.twist.angular.y = 0.0;
    odometry_msg.twist.twist.angular.z = W;

    //publish the message
    this->odometry_publisher.publish(odometry_msg);
}

//broadcast TF on odom --> base_link
void Subscriber::odometryBroadcast(float x, float y, float theta, ros::Time stamp){

    geometry_msgs::TransformStamped odometry_tf;

    odometry_tf.header.stamp = stamp;
    odometry_tf.header.frame_id = "odom";
    odometry_tf.child_frame_id = "base_link";

    odometry_tf.transform.translation.x = x;
    odometry_tf.transform.translation.y = y;
    odometry_tf.transform.translation.z = 0.0;

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, theta);
    odometry_tf.transform.rotation.x = q.x();
    odometry_tf.transform.rotation.y = q.y();
    odometry_tf.transform.rotation.z = q.z();
    odometry_tf.transform.rotation.w = q.w();

    this->br.sendTransform(odometry_tf);
}

//set new current position after service request
bool Subscriber::setServicePosition(first_project::setPos::Request  &req, first_project::setPos::Response &res){
    setPosition(req.x, req.y, req.theta);
    return true;
}

//change current approximation after dynamic_reconfigure request
void Subscriber::approximationChange(int approximation){
    if(approximation == 0)
        ROS_INFO("Approximation changed: EULER");
    else
        ROS_INFO("Approximation changed: RUNGE-KUTTA");
    this->approximationType = approximation;
}

//change wheel parameters after dynamic_reconfigure request
void Subscriber::wheelParametersChange(float r, float l, float w, int N, int level){
    switch(level){
        case 0:
            this->r = r;
            ROS_INFO("r parameter changed: %f", this->r);
            break;
        case 1:
            this->l = l;
            ROS_INFO("l parameter changed: %f", this->l);
            break;
        case 2:
            this->w = w;
            ROS_INFO("w parameter changed: %f", this->w);
            break;
        case 3:
            this->N = N;
            ROS_INFO("N parameter changed: %d", this->N);
            break;
    }
    ROS_INFO("r: %f, l: %f, w: %f, N: %d", this->r, this->l, this->w, this->N);
}

//set current robot position
void Subscriber::setPosition(float x, float y, float theta){
    this->x_old = x;
    this->y_old = y;
    this->theta_old = theta;
    ROS_INFO("NEW POSE SETTED");
    ROS_INFO("My pose_position: x = %f, y = %f, theta = %f", x_old, y_old, theta_old);
}