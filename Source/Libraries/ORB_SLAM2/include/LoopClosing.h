/**
 * This file is part of ORB-SLAM2.
 *
 * Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University
 * of Zaragoza) For more information see <https://github.com/raulmur/ORB_SLAM2>
 *
 * ORB-SLAM2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ORB-SLAM2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOOPCLOSING_H
#define LOOPCLOSING_H

#include "KeyFrame.h"
#include "LocalMapping.h"
#include "Map.h"
#include "ORBVocabulary.h"
#include "Tracking.h"

#include "KeyFrameDatabase.h"

#include "g2o/types/sim3/types_seven_dof_expmap.h"
#include <mutex>
#include <thread>

namespace ORB_SLAM2 {

class Tracking;
class LocalMapping;
class KeyFrameDatabase;

class LoopClosing {

public:
  typedef std::pair<std::set<KeyFrame *>, int> ConsistentGroup;
  typedef std::map<KeyFrame *, g2o::Sim3, std::less<KeyFrame *>, Eigen::aligned_allocator<std::pair<KeyFrame *const, g2o::Sim3>>> KeyFrameAndPose;

public:
  int Ntype;

  LoopClosing(Map *pMap, std::vector<KeyFrameDatabase *> pDB, std::vector<ORBVocabulary *> pVoc,
              const bool bFixScale, int Ntype);

  void SetTracker(Tracking *pTracker);

  void SetLocalMapper(LocalMapping *pLocalMapper);

  // Main function
  void Run();

  void InsertKeyFrame(KeyFrame *pKF);

  void RequestReset();

  // This function will run in a separate thread
  void RunGlobalBundleAdjustmentMultiChannels(unsigned long nLoopKF);

  bool isRunningGBA() {
    std::unique_lock<std::mutex> lock(mMutexGBA);
    return mbRunningGBA;
  }
  bool isFinishedGBA() {
    std::unique_lock<std::mutex> lock(mMutexGBA);
    return mbFinishedGBA;
  }

  void RequestFinish();

  bool isFinished();

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

protected:
  bool CheckNewKeyFrames();

  bool DetectLoop(const int Ftype);
  bool ComputeSim3(const int Ftype);
  void CorrectLoop(const int Ftype);

  void SearchAndFuse(const KeyFrameAndPose &CorrectedPosesMap, const int Ftype);

  void ResetIfRequested();
  bool mbResetRequested;
  std::mutex mMutexReset;

  bool CheckFinish();
  void SetFinish();
  bool mbFinishRequested;
  bool mbFinished;
  std::mutex mMutexFinish;

  Map *mpMap;
  Tracking *mpTracker;

  std::vector<KeyFrameDatabase *> mpKeyFrameDB;
  std::vector<ORBVocabulary *> mpVocabulary;

  LocalMapping *mpLocalMapper;

  std::list<KeyFrame *> mlpLoopKeyFrameQueue;

  std::mutex mMutexLoopQueue;

  // Loop detector parameters
  float mnCovisibilityConsistencyTh;

  // Loop detector variables
  KeyFrame *mpCurrentKF;
  KeyFrame *mpMatchedKF;
  std::vector<ConsistentGroup> mvConsistentGroups;
  std::vector<KeyFrame *> mvpEnoughConsistentCandidates;
  std::vector<KeyFrame *> mvpCurrentConnectedKFs;
  std::vector<MapPoint *> mvpCurrentMatchedPoints;
  std::vector<MapPoint *> mvpLoopMapPoints;
  cv::Mat mScw;
  g2o::Sim3 mg2oScw;

  long unsigned int mLastLoopKFid;

  // Variables related to Global Bundle Adjustment
  bool mbRunningGBA;
  bool mbFinishedGBA;
  bool mbStopGBA;
  std::mutex mMutexGBA;
  std::thread *mpThreadGBA;

  // Fix scale in the stereo/RGB-D case
  bool mbFixScale;

  int mnFullBAIdx;
};

} // namespace ORB_SLAM2

#endif // LOOPCLOSING_H
