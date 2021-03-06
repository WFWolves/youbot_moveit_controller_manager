/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, Arne Hitzmann
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Ioan A. Sucan nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Arne Hitzmann */

#include <ros/ros.h>
#include <iostream>
#include <moveit/controller_manager/controller_manager.h>
#include <sensor_msgs/JointState.h>
#include <pluginlib/class_list_macros.h>
#include <map>
#include <control_msgs/FollowJointTrajectoryActionGoal.h>

namespace moveit_youbot_controller_manager
{

class YouBotControllerHandle : public moveit_controller_manager::MoveItControllerHandle
{
public:
  YouBotControllerHandle(const std::string &name, ros::NodeHandle &nh, const std::vector<std::string> &joints) :
    moveit_controller_manager::MoveItControllerHandle(name),
    nh_(nh),
    joints_(joints)
  {
    lastResult = moveit_controller_manager::ExecutionStatus::SUCCEEDED;
    pub_ = nh.advertise<control_msgs::FollowJointTrajectoryActionGoal> ("/arm_controller/follow_joint_trajectory/goal", 1);
    std::stringstream ss;
    ss << "youBot controller '" << name << "' with joints [ ";
    for (std::size_t i = 0 ; i < joints.size() ; ++i)
      ss << joints[i] << " ";
    ss << "]";
    ROS_INFO("%s", ss.str().c_str());

  }
  
  void getJoints(std::vector<std::string> &joints) const
  {
    joints = joints_;
  }
  
  virtual bool sendTrajectory(const moveit_msgs::RobotTrajectory &t)
  {
    ROS_INFO("Execution of trajectory");
    if (!t.joint_trajectory.points.empty())
    {
      control_msgs::FollowJointTrajectoryActionGoal msg;

      msg.goal.trajectory.points = t.joint_trajectory.points;
      msg.goal.trajectory.header = t.joint_trajectory.header;
      msg.goal.trajectory.joint_names = t .joint_trajectory.joint_names;

      /*Debugging
      for(std::size_t i = 0; i < t.joint_trajectory.points.size(); i++) {
          std::cout<<"-----------------------------------------"<<std::endl;
          //Execute trajectory
          for(std::size_t j = 0; j < t.joint_trajectory.points[i].positions.size(); j++) {
            std::cout<<"Position:"<<t.joint_trajectory.points[i].positions[j]<<std::endl;
            std::cout<<"Velocity:"<<t.joint_trajectory.points[i].velocities[j]<<std::endl;
          }

          //Wait till joints reached desired Position
      }*/

      pub_.publish(msg);

    } else {
    	ROS_INFO("Received empty trajectory");
      return false;
    }
    
    return true;
  }
  
  virtual bool cancelExecution()
  {   
    ROS_INFO("Trajectory execution cancel");


    //Read current position of arm joints and send move command with these values to stop the movement

    return true;
  }
  
  virtual bool waitForExecution(const ros::Duration &d)
  {
    ros::Time start = ros::Time::now();
    ros::Time check;
      do{
        check = ros::Time::now();
      } while(check - start < d);
    return true;
  }
  
  virtual moveit_controller_manager::ExecutionStatus getLastExecutionStatus()
  {
      return moveit_controller_manager::ExecutionStatus(lastResult);
  }


  void jointStateCallback(const sensor_msgs::JointState& js){
    ROS_INFO("jointCB");
    joint_states = js;
  }
  
private:
  sensor_msgs::JointState joint_states;
  ros::Publisher pub_;
  moveit_controller_manager::ExecutionStatus lastResult;
  ros::NodeHandle nh_;
  std::vector<std::string> joints_;
  
};


class YouBotMoveItControllerManager : public moveit_controller_manager::MoveItControllerManager
{
public:

  YouBotMoveItControllerManager() : node_handle_("~")
  {
    if (!node_handle_.hasParam("controller_list"))
    {
      ROS_ERROR_STREAM("MoveItControllerManager: No controller_list specified.");
      return;
    }
    
    XmlRpc::XmlRpcValue controller_list;
    node_handle_.getParam("controller_list", controller_list);
    if (controller_list.getType() != XmlRpc::XmlRpcValue::TypeArray)
    {
      ROS_ERROR("MoveItControllerManager: controller_list should be specified as an array");
      return;
    }
    
    /* actually create each controller */
    for (int i = 0 ; i < controller_list.size() ; ++i)
    {
      if (!controller_list[i].hasMember("name") || !controller_list[i].hasMember("joints"))
      {
        ROS_ERROR("MoveItControllerManager: Name and joints must be specifed for each controller");
        continue;
      }
      
      try
      {
        std::string name = std::string(controller_list[i]["name"]);
        
        if (controller_list[i]["joints"].getType() != XmlRpc::XmlRpcValue::TypeArray)
        {
          ROS_ERROR_STREAM("MoveItControllerManager: The list of joints for controller " << name << " is not specified as an array");
          continue;
        }
        std::vector<std::string> joints;
        for (int j = 0 ; j < controller_list[i]["joints"].size() ; ++j)
          joints.push_back(std::string(controller_list[i]["joints"][j]));

        controllers_[name].reset(new YouBotControllerHandle(name, node_handle_, joints));
      }
      catch (...)
      {
        ROS_ERROR("MoveItControllerManager: Unable to parse controller information");
      }
    }
  }
  
  virtual ~YouBotMoveItControllerManager()
  {
  }

  /*
   * Get a controller, by controller name (which was specified in the controllers.yaml
   */
  virtual moveit_controller_manager::MoveItControllerHandlePtr getControllerHandle(const std::string &name)
  {
    std::map<std::string, moveit_controller_manager::MoveItControllerHandlePtr>::const_iterator it = controllers_.find(name);
    if (it != controllers_.end())
      return it->second;
    else
      ROS_FATAL_STREAM("No such controller: " << name);
    return moveit_controller_manager::MoveItControllerHandlePtr();
  }

  /*
   * Get the list of controller names.
   */
  virtual void getControllersList(std::vector<std::string> &names)
  {
    for (std::map<std::string, moveit_controller_manager::MoveItControllerHandlePtr>::const_iterator it = controllers_.begin() ; it != controllers_.end() ; ++it)
      names.push_back(it->first);
    ROS_INFO_STREAM("Returned " << names.size() << " controllers in list");
  }

  /*
   * This plugin assumes that all controllers are already active -- and if they are not, well, it has no way to deal with it anyways!
   */
  virtual void getActiveControllers(std::vector<std::string> &names)
  {
    getControllersList(names);
  }

  /*
   * Controller must be loaded to be active, see comment above about active controllers...
   */
  virtual void getLoadedControllers(std::vector<std::string> &names)
  {
    getControllersList(names);
  }

  /*
   * Get the list of joints that a controller can control.
   */
  virtual void getControllerJoints(const std::string &name, std::vector<std::string> &joints)
  {
    std::map<std::string, moveit_controller_manager::MoveItControllerHandlePtr>::const_iterator it = controllers_.find(name);
    if (it != controllers_.end())
    {
      static_cast<YouBotControllerHandle*>(it->second.get())->getJoints(joints);
    }
    else
    {
      ROS_WARN("The joints for controller '%s' are not known. Perhaps the controller configuration is not loaded on the param server?", name.c_str());
      joints.clear();
    }
  }

  /*
   * Controllers are all active and default.
   */
  virtual moveit_controller_manager::MoveItControllerManager::ControllerState getControllerState(const std::string &name)
  {
    moveit_controller_manager::MoveItControllerManager::ControllerState state;
    state.active_ = true;
    state.default_ = true;
    return state;
  }

  /* Cannot switch our controllers */
  virtual bool switchControllers(const std::vector<std::string> &activate, const std::vector<std::string> &deactivate) { return false; }

protected:

  ros::NodeHandle node_handle_;
  std::map<std::string, moveit_controller_manager::MoveItControllerHandlePtr> controllers_;
};

} // end namespace moveit_fake_controller_manager

PLUGINLIB_EXPORT_CLASS(moveit_youbot_controller_manager::YouBotMoveItControllerManager,
                       moveit_controller_manager::MoveItControllerManager);
