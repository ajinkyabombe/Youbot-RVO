/**
* Created by Alejandro Bordallo
* \file   Tracker.cpp
* \brief  Deals with the Agent Tracker information
*/

#include "Tracker.h"

namespace hrvo {

  // Tracker::Tracker(Environment *environment) : environment_(environment)
  Tracker::Tracker()
  {
    Targsub = nh_.subscribe("/agent_1/PTrackingBridge/targetEstimations", 1, &Tracker::receiveTrackerData, this);
    ROS_INFO("Suscribing to TargetEstimations");
    trackOtherAgents_ = false;
  }

  Tracker::~Tracker()
  {

  }

  void Tracker::updateTracker()
  {
    if (!msg_.identities.empty())
    {
      std::size_t numAgents = msg_.identities.size();

      std::map<int, std::size_t> ids = this->getTrackerIDs();
      DEBUG(environment_->getStringActorID()<<":"<<std::endl);

      // Check if trackers for existing agents are still active
      for(std::map<int, std::size_t>::iterator iter = trackedAgents_.begin(); iter != trackedAgents_.end(); ++iter)
      {
        int TrackID = iter->first;
        std::size_t AgentID = iter->second;
        bool found = false;

          
          DEBUG("Tracker" << TrackID << " for Agent" << AgentID);
          for (int i = 0; i < ids.size(); ++i)
          {
            if (ids[i] == TrackID)
            {
              DEBUG(" active" << std::endl);
              found = true;
            }
          }

          if (TrackID != robotTrackerID_ && AgentID == THIS_ROBOT)
          {
            ERR(environment_->getStringActorID() << " already has tracker " << robotTrackerID_ << " instead of " << TrackID << std::endl);
            trackedAgents_.erase(iter);
          }

          if (!found || ids.size() == 0)
          {
            DEBUG(" inactive" << std::endl);
            if (AgentID != THIS_ROBOT)
            {
              planner_->setAgentPosition(AgentID, EXIT);
              planner_->setAgentVelocity(AgentID, STOP);
              planner_->setAgentType(AgentID, INACTIVE);
            }
            // planner_->agents_.erase(iterator __position);
            trackedAgents_.erase(iter);
            trackerCompOdom_.erase(TrackID);
          }
      }

      // Deactivate any pedestrian agents who are no longer being tracked
      for (std::size_t AgentNo = 1; AgentNo < planner_->getNumAgents(); ++AgentNo)
      {
        bool AgentTracked = false;
        for(std::map<int, std::size_t>::iterator iter = trackedAgents_.begin(); iter != trackedAgents_.end(); ++iter)
        {
          if (iter->second == AgentNo)  { AgentTracked = true;}
        }
        if (!AgentTracked && planner_->getAgentType(AgentNo) == PERSON )
        {
            planner_->setAgentPosition(AgentNo, EXIT);
            planner_->setAgentVelocity(AgentNo, STOP);
            planner_->setAgentType(AgentNo, INACTIVE);
        }
      }

      // Update existing tracked agents or create new agents for new trackers
      for (std::size_t i = 0; i < numAgents; ++i)
      {
        int TrackerID = msg_.identities[i];
        std::string sid = intToString(TrackerID);
        Vector2 agentPos = EXIT;
        if (INVERT_X)
        {
          agentPos = Vector2(-1 * msg_.positions[i].x, msg_.positions[i].y);
        }
        else
        {
          agentPos = Vector2(msg_.positions[i].x, msg_.positions[i].y);
        }

        Vector2 agentVel = STOP;
        // agentVel = Vector2(-1 * msg_.velocities[i].x, msg_.velocities[i].y);
        if (INVERT_X)
        {
          agentVel = Vector2(-1 * msg_.averagedVelocities[i].x, msg_.averagedVelocities[i].y);
        }
        else
        {
        agentVel = Vector2(msg_.averagedVelocities[i].x, msg_.averagedVelocities[i].y);
        }

        if (ASSIGN_TRACKER_WHEN_ALONE && (trackedAgents_.empty() || numAgents == 1))
        { 
          this->setAgentTracker(TrackerID, THIS_ROBOT);
          planner_->resetOdomPosition();
          DEBUG("Assigned lone tracker" << TrackerID << "to "<< environment_->getStringActorID() << std::endl);
        }

        // If TrackerID has not been assigned to an agent yet, reassing to robot or create new agent
        if (trackOtherAgents_ && trackedAgents_.find(TrackerID)==trackedAgents_.end() && trackedAgents_.size() < MAX_NO_TRACKED_AGENTS )  
        { // TODO: Limit number of created agents
          if (TrackerID == robotTrackerID_ && ENABLE_PLANNER)
          { 
            this->setAgentTracker(TrackerID, THIS_ROBOT);
            planner_->resetOdomPosition();
            DEBUG("Re-assigned robot tracker" << TrackerID << " to "<< environment_->getStringActorID() << std::endl);
          }
          else
          {
            trackedAgents_[TrackerID] = environment_->addPedestrianAgent("TrackedPerson" + sid, agentPos, environment_->addPlannerGoal(agentPos));
            DEBUG("New agent" << trackedAgents_[TrackerID] << " with tracker" << sid << std::endl);
          }
        }

        // If TrackerID has been assigned already, update agent information
        if ((trackedAgents_[TrackerID] == THIS_ROBOT) && !ONLY_ODOMETRY)
        {
          planner_->setOdomNeeded(false);
          planner_->setAgentPosition(THIS_ROBOT, agentPos + trackerOffset);
        }
        else if (trackedAgents_.find(TrackerID)!=trackedAgents_.end())
        {

          // float avgVel = this->calculateAvgMaxSpeeds(TrackerID, agentVel);
          std::pair<float, Vector2> avgSpeedVel = this->calculateAvgMaxSpeeds(trackedAgents_[TrackerID], agentVel);
          float avgSpeed = avgSpeedVel.first;
          Vector2 avgVel = avgSpeedVel.second;

          planner_->setAgentPosition(trackedAgents_[TrackerID], agentPos + trackerOffset);
          // planner_->setAgentVelocity(trackedAgents_[TrackerID], agentVel);
          planner_->setAgentVelocity(trackedAgents_[TrackerID], avgVel);
          planner_->setAgentPrefSpeed(trackedAgents_[TrackerID], avgSpeed);
          planner_->setAgentMaxSpeed(trackedAgents_[TrackerID], maxSpeed_[trackedAgents_[TrackerID]]);
          // planner_->setAgentMaxAcceleration(trackedAgents_[TrackerID], maxAcc_);
        }

        // Increase comparison magnitude proportional to the difference between odometry and tracker positions
        if (ENABLE_PLANNER)
        {
            float odomdiff = sqrdiff(planner_->getOdomPosition(), agentPos);
            // if (planner_->getAgentType(trackedAgents_[TrackerID]) != INACTIVE)
            // {
              trackerCompOdom_[TrackerID].insert(trackerCompOdom_[TrackerID].begin(), odomdiff);
              trackerCompOdom_[TrackerID].resize(TRACKER_ODOM_COMPARISONS);
              DEBUG("Tracker" << TrackerID << " Pos " << agentPos << std::endl);
              // DEBUG("CompOdom " << odomdiff << std::endl);
            // }
        }
      }

      // Compare robot odometry with tracker positions in order to reacquire stolen tracker
      if (ENABLE_PLANNER)
      {
        int TargetTrackerID = -1; // Initialisation values
        float minComp = 10.0f;
        std::map<int, float> odomSums;
        // Add up histories for all tracker odometry comparisons
        for(std::map<int, std::vector<float> >::iterator iter = trackerCompOdom_.begin(); iter != trackerCompOdom_.end(); ++iter)
        {
          int TrackerID = iter->first;
          std::vector<float> OdomComparison = iter->second;
          if (planner_->getAgentType(trackedAgents_[TrackerID]) != INACTIVE)
          {
            odomSums[TrackerID] = 0.0f;
            for(std::vector<float>::iterator iter = OdomComparison.begin(); iter != OdomComparison.end(); ++iter)
            {
              odomSums[TrackerID] += (*iter); 
            }
          }
        }
        for(std::map<int, float>::iterator iter = odomSums.begin(); iter != odomSums.end(); ++iter)
        {
          int TrackerID = iter->first;
          float odomSum = iter->second;
          // DEBUG("minComp " << minComp << " odomSum " << odomSum << " TrackerID " << TrackerID);
          if (odomSum < minComp)
          {
            TargetTrackerID = TrackerID;
            minComp = odomSum;
          }
        }

        if (TargetTrackerID == -1)
        { ERR("ERROR: NO TRACKER CAN BE ASSIGNED TO " << environment_->getStringActorID() << std::endl);}
        else if (trackedAgents_[TargetTrackerID] != THIS_ROBOT && trackerCompOdom_[TargetTrackerID].size() >= TRACKER_ODOM_COMPARISONS)
        {
          ERR("Tracker " << TargetTrackerID << "belongs to " << environment_->getStringActorID() << " instead of " << trackedAgents_[TargetTrackerID] << std::endl);
          // If smallest is not the one assigned to robot, then substitute and delete extra tracked agent
          // planner_->setAgentPosition(trackedAgents_[robotTrackerID_], STOP);
          // planner_->setAgentVelocity(trackedAgents_[robotTrackerID_], STOP);
          // planner_->setAgentType(trackedAgents_[robotTrackerID_], INACTIVE);
          trackedAgents_.erase(robotTrackerID_);
          trackedAgents_[TargetTrackerID] = THIS_ROBOT;
          robotTrackerID_ = TargetTrackerID;
        }
        // Reset all comparisons
      
      }
      DEBUG(std::endl);

    }

  }

  std::map<int, std::size_t> Tracker::getTrackerIDs()
  {
    std::size_t numAgents = msg_.identities.size();

    std::map<int, std::size_t> ids;     // First = Index, Second = Tracker ID
    for (int i = 0; i < numAgents; ++i)
    {
      ids[i] = msg_.identities[i];
    }
    return ids;
  }

  void Tracker::receiveTrackerData(const PTrackingBridge::TargetEstimations::ConstPtr& msg)
  {
    msg_ = *msg;
  }

  void Tracker::setAgentTracker(int TrackerID, std::size_t AgentID)  
  {
    trackedAgents_[TrackerID] = AgentID;
    if (AgentID == THIS_ROBOT) 
        {
          robotTrackerID_ = TrackerID;
        }
  }

  std::pair<float, Vector2> Tracker::calculateAvgMaxSpeeds(int AgentID, Vector2 AgentVel)
  {
    // TODO: Implement discounted sum over past Sum:(1 - disc)^t-1 * Value  where disc(0 < disc =< 1)
    agentVelHistory_[AgentID].insert(agentVelHistory_[AgentID].begin(), AgentVel);
    agentVelHistory_[AgentID].resize(VELOCITY_AVERAGE_WINDOW);
    std::size_t VelHist = agentVelHistory_[AgentID].size();

    float avgSpeed = 0.0f;
    float avgVelx = 0.0f;
    float avgVely = 0.0f;
    for(std::vector<Vector2>::iterator vel = agentVelHistory_[AgentID].begin(); vel != agentVelHistory_[AgentID].end(); ++vel)
    {
      avgSpeed += abs(*vel);
      avgVelx += (*vel).getX();
      avgVely += (*vel).getY();
    }
    avgSpeed = avgSpeed / agentVelHistory_[AgentID].size();
    Vector2 avgVel = Vector2(avgVelx / VelHist, avgVely / VelHist);
    if (avgSpeed > maxSpeed_[AgentID])
    {maxSpeed_[AgentID] = avgSpeed;}

    // DEBUG("Agent" << AgentID << " AvgVel="<<avgSpeed<<" maxVel="<<maxSpeed_[AgentID]<<std::endl);

    return std::make_pair(avgSpeed, avgVel);
  }

}