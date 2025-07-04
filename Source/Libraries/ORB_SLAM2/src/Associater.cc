#include "Associater.h"

#include <limits.h>

#include "DBoW2/FeatureVector.h"

#include <stdint.h>

using namespace ::std;

namespace ORB_SLAM2 {

const int Associater::TH_HIGH = 100;
const int Associater::TH_LOW = 50;
const int Associater::HISTO_LENGTH = 30;

Associater::Associater(float nnratio, bool checkOri)
    : mfNNratio(nnratio), mbCheckOrientation(checkOri) {}

// Rewrite, it should be used in the trackmotionmodel
int Associater::SearchByProjection(Frame &CurrentFrame, const Frame &LastFrame, const float th, const bool bMono, const int Ftype) {
  
  int nmatches = 0;
  
  // step 1 build rotation histogram (to check rotation consistency)
  vector<int> rotHist[HISTO_LENGTH];
  for (int i = 0; i < HISTO_LENGTH; i++)
    rotHist[i].reserve(500);
  const float factor = 1.0f / HISTO_LENGTH;

  // step 2 calcualte translation of current frame and last frame 
  // pose of current frame
  const cv::Mat Rcw = CurrentFrame.mTcw.rowRange(0, 3).colRange(0, 3);
  const cv::Mat tcw = CurrentFrame.mTcw.rowRange(0, 3).col(3);

  // translation from current frame to world coordinate
  const cv::Mat twc = -Rcw.t() * tcw;

  // pose of last frame 
  const cv::Mat Rlw = LastFrame.mTcw.rowRange(0, 3).colRange(0, 3);
  const cv::Mat tlw = LastFrame.mTcw.rowRange(0, 3).col(3);

  // translation from current frame to last frame
  const cv::Mat tlc = Rlw * twc + tlw;

  // judge forward or backward
  const bool bForward = tlc.at<float>(2) > CurrentFrame.mb && !bMono;
  const bool bBackward = -tlc.at<float>(2) > CurrentFrame.mb && !bMono;

  // step 3 project the mappoints created by last frame to current frame, calcualte its  u and v in pixel
  for (int i = 0; i < LastFrame.Channels[Ftype].N; i++) {
    MapPoint *pMP = LastFrame.Channels[Ftype].mvpMapPoints[i];
  
    if (pMP) {
      if (!LastFrame.Channels[Ftype].mvbOutlier[i]) {
        // Project
        cv::Mat x3Dw = pMP->GetWorldPos();
        cv::Mat x3Dc = Rcw * x3Dw + tcw;

        const float xc = x3Dc.at<float>(0);
        const float yc = x3Dc.at<float>(1);
        const float invzc = 1.0 / x3Dc.at<float>(2);

        if (invzc < 0)
          continue;
        
        float u = CurrentFrame.fx * xc * invzc + CurrentFrame.cx;
        float v = CurrentFrame.fy * yc * invzc + CurrentFrame.cy;

        if (u < CurrentFrame.mnMinX || u > CurrentFrame.mnMaxX)
          continue;
        if (v < CurrentFrame.mnMinY || v > CurrentFrame.mnMaxY)
          continue;

        int nLastOctave = LastFrame.Channels[Ftype].mvKeys[i].octave;

        // Search in a window. Size depends on scale
        float radius = th * CurrentFrame.mvScaleFactors[nLastOctave];

        vector<size_t> vIndices2;

        if (bForward)
          vIndices2 = CurrentFrame.GetFeaturesInArea(Ftype, u, v, radius, nLastOctave);
        else if (bBackward)
          vIndices2 =
              CurrentFrame.GetFeaturesInArea(Ftype ,u, v, radius, 0, nLastOctave);
        else
          vIndices2 = CurrentFrame.GetFeaturesInArea(Ftype, u, v, radius, nLastOctave - 1, nLastOctave + 1);

        if (vIndices2.empty())
          continue;

        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = 256;
        int bestIdx2 = -1;

        for (vector<size_t>::const_iterator vit = vIndices2.begin(), vend = vIndices2.end(); vit != vend; vit++) {
          const size_t i2 = *vit;
          if (CurrentFrame.Channels[Ftype].mvpMapPoints[i2])
            if (CurrentFrame.Channels[Ftype].mvpMapPoints[i2]->Observations() > 0)
              continue;

          if (CurrentFrame.Channels[Ftype].mvuRight[i2] > 0) {
            const float ur = u - CurrentFrame.mbf * invzc;
            const float er = fabs(ur - CurrentFrame.Channels[Ftype].mvuRight[i2]);
            if (er > radius)
              continue;
          }

          const cv::Mat &d = CurrentFrame.Channels[Ftype].mDescriptors.row(i2);

          const int dist = DescriptorDistance(dMP, d);

          if (dist < bestDist) {
            bestDist = dist;
            bestIdx2 = i2;
          }
        }

        if (bestDist <= TH_HIGH) {
          CurrentFrame.Channels[Ftype].mvpMapPoints[bestIdx2] = pMP;
          nmatches++;

          if (mbCheckOrientation) {
            float rot = LastFrame.Channels[Ftype].mvKeysUn[i].angle - CurrentFrame.Channels[Ftype].mvKeysUn[bestIdx2].angle;
            if (rot < 0.0)
              rot += 360.0f;
            int bin = round(rot * factor);
            if (bin == HISTO_LENGTH)
              bin = 0;
            assert(bin >= 0 && bin < HISTO_LENGTH);
            rotHist[bin].push_back(bestIdx2);
          }
        }
      }
    }
  }

  // Apply rotation consistency
  if (mbCheckOrientation) {
    int ind1 = -1;
    int ind2 = -1;
    int ind3 = -1;

    ComputeThreeMaxima(rotHist, HISTO_LENGTH, ind1, ind2, ind3);

    for (int i = 0; i < HISTO_LENGTH; i++) {
      if (i != ind1 && i != ind2 && i != ind3) {
        for (size_t j = 0, jend = rotHist[i].size(); j < jend; j++) {
          CurrentFrame.Channels[Ftype].mvpMapPoints[rotHist[i][j]] = static_cast<MapPoint *>(NULL);
          nmatches--;
        }
      }
    }
  }
  
  return nmatches;
}

// used in trackwithlocalmap
int Associater::SearchByProjection(Frame &F, const vector<MapPoint*> &vpMapPoints, const float th) {
  int nmatches = 0;

  const bool bFactor = th != 1.0;

  for (size_t iMP = 0; iMP < vpMapPoints.size(); iMP++) {
    MapPoint *pMP = vpMapPoints[iMP];
    if (!pMP->mbTrackInView)
      continue;

    if (pMP->isBad())
      continue;

    int Ftype = pMP->GetFeatureType();
    if (Ftype == -1)
      continue;

    const int &nPredictedLevel = pMP->mnTrackScaleLevel;

    // The size of the window will depend on the viewing direction
    float r = RadiusByViewingCos(pMP->mTrackViewCos);

    if (bFactor)
      r *= th;

    const vector<size_t> vIndices = F.GetFeaturesInArea(Ftype, pMP->mTrackProjX, pMP->mTrackProjY, r * F.mvScaleFactors[nPredictedLevel],
                                                        nPredictedLevel - 1, nPredictedLevel);

    if (vIndices.empty())
      continue;

    const cv::Mat MPdescriptor = pMP->GetDescriptor();

    int bestDist = 256;
    int bestLevel = -1;
    int bestDist2 = 256;
    int bestLevel2 = -1;
    int bestIdx = -1;

    // Get best and second matches with near keypoints
    for (vector<size_t>::const_iterator vit = vIndices.begin(), vend = vIndices.end(); vit != vend; vit++) {
      const size_t idx = *vit;

      if (F.Channels[Ftype].mvpMapPoints[idx])
        if (F.Channels[Ftype].mvpMapPoints[idx]->Observations() > 0)
          continue;

      if (F.Channels[Ftype].mvuRight[idx] > 0) {
        const float er = fabs(pMP->mTrackProjXR - F.Channels[Ftype].mvuRight[idx]);
        if (er > r * F.mvScaleFactors[nPredictedLevel])
          continue;
      }

      const cv::Mat &d = F.Channels[Ftype].mDescriptors.row(idx);

      const int dist = DescriptorDistance(MPdescriptor, d);

      if (dist < bestDist) {
        bestDist2 = bestDist;
        bestDist = dist;
        bestLevel2 = bestLevel;
        bestLevel = F.Channels[Ftype].mvKeysUn[idx].octave;
        bestIdx = idx;
      } else if (dist < bestDist2) {
        bestLevel2 = F.Channels[Ftype].mvKeysUn[idx].octave;
        bestDist2 = dist;
      }
    }

    // Apply ratio to second match (only if best and second are in the same
    // scale level)
    if (bestDist <= TH_HIGH) {
      if (bestLevel == bestLevel2 && bestDist > mfNNratio * bestDist2)
        continue;

      F.Channels[Ftype].mvpMapPoints[bestIdx] = pMP;
      nmatches++;
    }
  }

  // std::cout << "Points in local map: " << vpMapPoints.size() << endl;
  // std::cout << "Matched points in local map: " << nmatches << endl;

  return nmatches;

}

int Associater::SearchByProjection(KeyFrame *pKF, cv::Mat Scw, const vector<MapPoint *> &vpPoints, vector<MapPoint *> &vpMatched, int th, const int Ftype) { 
  // Get Calibration Parameters for later projection
  const float &fx = pKF->fx;
  const float &fy = pKF->fy;
  const float &cx = pKF->cx;
  const float &cy = pKF->cy;

  // Decompose Scw 
  cv::Mat sRcw = Scw.rowRange(0, 3).colRange(0, 3);
  const float scw = sqrt(sRcw.row(0).dot(sRcw.row(0)));
  cv::Mat Rcw = sRcw / scw;
  cv::Mat tcw = Scw.rowRange(0, 3).col(3) / scw;
  cv::Mat Ow = -Rcw.t() * tcw;

  // Set of MapPoints already found in the KeyFrame
  set<MapPoint *> spAlreadyFound(vpMatched.begin(), vpMatched.end());
  spAlreadyFound.erase(static_cast<MapPoint *>(NULL));

  int nmatches = 0;

  // For each Candidate MapPoint Project and Match
  for (int iMP = 0, iendMP = vpPoints.size(); iMP < iendMP; iMP++) {
    MapPoint *pMP = vpPoints[iMP];

    // Discard Bad MapPoints and already found
    if (pMP->isBad() || spAlreadyFound.count(pMP))
      continue;

    // Get 3D Coords.
    cv::Mat p3Dw = pMP->GetWorldPos();

    // Transform into Camera Coords.
    cv::Mat p3Dc = Rcw * p3Dw + tcw;

    // Depth must be positive
    if (p3Dc.at<float>(2) < 0.0)
      continue;

    // Project into Image
    const float invz = 1 / p3Dc.at<float>(2);
    const float x = p3Dc.at<float>(0) * invz;
    const float y = p3Dc.at<float>(1) * invz;

    const float u = fx * x + cx;
    const float v = fy * y + cy;

    // Point must be inside the image
    if (!pKF->IsInImage(u, v))
      continue;

    // Depth must be inside the scale invariance region of the point
    const float maxDistance = pMP->GetMaxDistanceInvariance();
    const float minDistance = pMP->GetMinDistanceInvariance();
    cv::Mat PO = p3Dw - Ow;
    const float dist = cv::norm(PO);

    if (dist < minDistance || dist > maxDistance)
      continue;

    // Viewing angle must be less than 60 deg
    cv::Mat Pn = pMP->GetNormal();

    if (PO.dot(Pn) < 0.5 * dist)
      continue;

    int nPredictedLevel = pMP->PredictScale(dist, pKF);

    // Search in a radius
    const float radius = th * pKF->mvScaleFactors[nPredictedLevel];

    const vector<size_t> vIndices = pKF->GetFeaturesInArea(u, v, radius, Ftype);

    if (vIndices.empty())
      continue;

    // Match to the most similar keypoint in the radius
    const cv::Mat dMP = pMP->GetDescriptor();

    int bestDist = 256;
    int bestIdx = -1;
    for (vector<size_t>::const_iterator vit = vIndices.begin(), vend = vIndices.end(); vit != vend; vit++) {
      const size_t idx = *vit;
      if (vpMatched[idx])
        continue;

      const int &kpLevel = pKF->Channels[Ftype].mvKeysUn[idx].octave;

      if (kpLevel < nPredictedLevel - 1 || kpLevel > nPredictedLevel)
        continue;

      const cv::Mat &dKF = pKF->Channels[Ftype].mDescriptors.row(idx);

      const int dist = DescriptorDistance(dMP, dKF);

      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = idx;
      }
    }

    if (bestDist <= TH_LOW) {
      vpMatched[bestIdx] = pMP;
      nmatches++;
    }
  }

  return nmatches;
}

int Associater::SearchByProjection(Frame &CurrentFrame, KeyFrame *pKF, const set<MapPoint *> &sAlreadyFound, 
                                   const float th, const int ORBdist, const int Ftype) {
  int nmatches = 0;

  const cv::Mat Rcw = CurrentFrame.mTcw.rowRange(0, 3).colRange(0, 3);
  const cv::Mat tcw = CurrentFrame.mTcw.rowRange(0, 3).col(3);
  const cv::Mat Ow = -Rcw.t() * tcw;

  // Rotation Histogram (to check rotation consistency)
  vector<int> rotHist[HISTO_LENGTH];
  for (int i = 0; i < HISTO_LENGTH; i++)
    rotHist[i].reserve(500);
  const float factor = 1.0f / HISTO_LENGTH;

  const vector<MapPoint *> vpMPs = pKF->GetMapPointMatches(Ftype);

  for (size_t i = 0, iend = vpMPs.size(); i < iend; i++) {
    MapPoint *pMP = vpMPs[i];

    if (pMP) {
      if (!pMP->isBad() && !sAlreadyFound.count(pMP)) {
        // Project
        cv::Mat x3Dw = pMP->GetWorldPos();
        cv::Mat x3Dc = Rcw * x3Dw + tcw;

        const float xc = x3Dc.at<float>(0);
        const float yc = x3Dc.at<float>(1);
        const float invzc = 1.0 / x3Dc.at<float>(2);

        const float u = CurrentFrame.fx * xc * invzc + CurrentFrame.cx;
        const float v = CurrentFrame.fy * yc * invzc + CurrentFrame.cy;

        if (u < CurrentFrame.mnMinX || u > CurrentFrame.mnMaxX)
          continue;
        if (v < CurrentFrame.mnMinY || v > CurrentFrame.mnMaxY)
          continue;

        // Compute predicted scale level
        cv::Mat PO = x3Dw - Ow;
        float dist3D = cv::norm(PO);

        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();

        // Depth must be inside the scale pyramid of the image
        if (dist3D < minDistance || dist3D > maxDistance)
          continue;

        int nPredictedLevel = pMP->PredictScale(dist3D, &CurrentFrame);

        // Search in a window
        const float radius = th * CurrentFrame.mvScaleFactors[nPredictedLevel];

        const vector<size_t> vIndices2 = CurrentFrame.GetFeaturesInArea(Ftype, u, v, radius, nPredictedLevel - 1, nPredictedLevel + 1);

        if (vIndices2.empty())
          continue;

        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = 256;
        int bestIdx2 = -1;

        for (vector<size_t>::const_iterator vit = vIndices2.begin(); vit != vIndices2.end(); vit++) {
          const size_t i2 = *vit;
          if (CurrentFrame.Channels[Ftype].mvpMapPoints[i2])
            continue;

          const cv::Mat &d = CurrentFrame.Channels[Ftype].mDescriptors.row(i2);

          const int dist = DescriptorDistance(dMP, d);

          if (dist < bestDist) {
            bestDist = dist;
            bestIdx2 = i2;
          }
        }

        if (bestDist <= ORBdist) {
          CurrentFrame.Channels[Ftype].mvpMapPoints[bestIdx2] = pMP;
          nmatches++;

          if (mbCheckOrientation) {
            float rot =
                pKF->Channels[Ftype].mvKeysUn[i].angle - CurrentFrame.Channels[Ftype].mvKeysUn[bestIdx2].angle;
            if (rot < 0.0)
              rot += 360.0f;
            int bin = round(rot * factor);
            if (bin == HISTO_LENGTH)
              bin = 0;
            assert(bin >= 0 && bin < HISTO_LENGTH);
            rotHist[bin].push_back(bestIdx2);
          }
        }
      }
    }
  }

  if (mbCheckOrientation) {
    int ind1 = -1;
    int ind2 = -1;
    int ind3 = -1;

    ComputeThreeMaxima(rotHist, HISTO_LENGTH, ind1, ind2, ind3);

    for (int i = 0; i < HISTO_LENGTH; i++) {
      if (i != ind1 && i != ind2 && i != ind3) {
        for (size_t j = 0, jend = rotHist[i].size(); j < jend; j++) {
          CurrentFrame.Channels[Ftype].mvpMapPoints[rotHist[i][j]] = NULL;
          nmatches--;
        }
      }
    }
  }

  return nmatches;
}

// used in trackwithkeyframe
int Associater::SearchByBoW(KeyFrame *pKF, Frame &F, vector<MapPoint *> &vpMapPointMatches, const int Ftype) {
  const vector<MapPoint *> vpMapPointsKF = pKF->GetMapPointMatches(Ftype);

  vpMapPointMatches = vector<MapPoint *>(F.Channels[Ftype].N, static_cast<MapPoint *>(NULL));

  // suppose the featvec equals the featvec in frame.featdata, mybe it was not updated
  const DBoW2::FeatureVector &vFeatVecKF = pKF->Channels[Ftype].mFeatVec;

  int nmatches = 0;

  vector<int> rotHist[HISTO_LENGTH];
  for (int i = 0; i < HISTO_LENGTH; i++)
    rotHist[i].reserve(500);
  const float factor = 1.0f / HISTO_LENGTH;

  DBoW2::FeatureVector::const_iterator KFit = vFeatVecKF.begin();
  DBoW2::FeatureVector::const_iterator Fit = F.Channels[Ftype].mFeatVec.begin();
  DBoW2::FeatureVector::const_iterator KFend = vFeatVecKF.end();
  DBoW2::FeatureVector::const_iterator Fend = F.Channels[Ftype].mFeatVec.end();

  while (KFit != KFend && Fit != Fend) {
    if (KFit->first == Fit->first) {
      const vector<unsigned int> vIndicesKF = KFit->second;
      const vector<unsigned int> vIndicesF = Fit->second;

      for (size_t iKF = 0; iKF < vIndicesKF.size(); iKF++) {
        const unsigned int realIdxKF = vIndicesKF[iKF];

        MapPoint *pMP = vpMapPointsKF[realIdxKF];

        if (!pMP)
          continue;

        if (pMP->isBad())
          continue;

        const cv::Mat &dKF = pKF->Channels[Ftype].mDescriptors.row(realIdxKF);

        int bestDist1 = 256;
        int bestIdxF = -1;
        int bestDist2 = 256;

        for (size_t iF = 0; iF < vIndicesF.size(); iF++) {
          const unsigned int realIdxF = vIndicesF[iF];

          if (vpMapPointMatches[realIdxF])
            continue;

          const cv::Mat &dF = F.Channels[Ftype].mDescriptors.row(realIdxF);

          const int dist = DescriptorDistance(dKF, dF);

          if (dist < bestDist1) {
            bestDist2 = bestDist1;
            bestDist1 = dist;
            bestIdxF = realIdxF;
          } else if (dist < bestDist2) {
            bestDist2 = dist;
          }
        }

        if (bestDist1 <= TH_LOW) {
          if (static_cast<float>(bestDist1) <
              mfNNratio * static_cast<float>(bestDist2)) {
            vpMapPointMatches[bestIdxF] = pMP;

            const cv::KeyPoint &kp = pKF->Channels[Ftype].mvKeysUn[realIdxKF];

            if (mbCheckOrientation) {
              float rot = kp.angle - F.Channels[Ftype].mvKeys[bestIdxF].angle;
              if (rot < 0.0)
                rot += 360.0f;
              int bin = round(rot * factor);
              if (bin == HISTO_LENGTH)
                bin = 0;
              assert(bin >= 0 && bin < HISTO_LENGTH);
              rotHist[bin].push_back(bestIdxF);
            }
            nmatches++;
          }
        }
      }

      KFit++;
      Fit++;
    } else if (KFit->first < Fit->first) {
      KFit = vFeatVecKF.lower_bound(Fit->first);
    } else {
      Fit = F.Channels[Ftype].mFeatVec.lower_bound(KFit->first);
    }
  }

  if (mbCheckOrientation) {
    int ind1 = -1;
    int ind2 = -1;
    int ind3 = -1;

    ComputeThreeMaxima(rotHist, HISTO_LENGTH, ind1, ind2, ind3);

    for (int i = 0; i < HISTO_LENGTH; i++) {
      if (i == ind1 || i == ind2 || i == ind3)
        continue;
      for (size_t j = 0, jend = rotHist[i].size(); j < jend; j++) {
        vpMapPointMatches[rotHist[i][j]] = static_cast<MapPoint *>(NULL);
        nmatches--;
      }
    }
  }

  return nmatches;
}

// used in the loopclosing 
int Associater::SearchByBoW(KeyFrame *pKF1, KeyFrame *pKF2, vector<MapPoint *> &vpMatches12, const int Ftype) {
  
  // step 1 : get key points of two channels
  const vector<cv::KeyPoint> &vKeysUn1 = pKF1->Channels[Ftype].mvKeysUn;
  const DBoW2::FeatureVector &vFeatVec1 = pKF1->Channels[Ftype].mFeatVec;
  const vector<MapPoint *> vpMapPoints1 = pKF1->GetMapPointMatches(Ftype);
  const cv::Mat &Descriptors1 = pKF1->Channels[Ftype].mDescriptors;

  const vector<cv::KeyPoint> &vKeysUn2 = pKF2->Channels[Ftype].mvKeysUn;
  const DBoW2::FeatureVector &vFeatVec2 = pKF2->Channels[Ftype].mFeatVec;
  const vector<MapPoint *> vpMapPoints2 = pKF2->GetMapPointMatches(Ftype);
  const cv::Mat &Descriptors2 = pKF2->Channels[Ftype].mDescriptors;

  vpMatches12 = vector<MapPoint *>(vpMapPoints1.size(), static_cast<MapPoint *>(NULL));
  vector<bool> vbMatched2(vpMapPoints2.size(), false);

  vector<int> rotHist[HISTO_LENGTH];
  for (int i = 0; i < HISTO_LENGTH; i++)
    rotHist[i].reserve(500);

  const float factor = 1.0f / HISTO_LENGTH;

  int nmatches = 0;

  DBoW2::FeatureVector::const_iterator f1it = vFeatVec1.begin();
  DBoW2::FeatureVector::const_iterator f2it = vFeatVec2.begin();
  DBoW2::FeatureVector::const_iterator f1end = vFeatVec1.end();
  DBoW2::FeatureVector::const_iterator f2end = vFeatVec2.end();

  while (f1it != f1end && f2it != f2end) {
    if (f1it->first == f2it->first) {
      for (size_t i1 = 0, iend1 = f1it->second.size(); i1 < iend1; i1++) {
        const size_t idx1 = f1it->second[i1];

        MapPoint *pMP1 = vpMapPoints1[idx1];
        if (!pMP1)
          continue;
        if (pMP1->isBad())
          continue;

        const cv::Mat &d1 = Descriptors1.row(idx1);

        int bestDist1 = 256;
        int bestIdx2 = -1;
        int bestDist2 = 256;

        for (size_t i2 = 0, iend2 = f2it->second.size(); i2 < iend2; i2++) {
          const size_t idx2 = f2it->second[i2];

          MapPoint *pMP2 = vpMapPoints2[idx2];

          if (vbMatched2[idx2] || !pMP2)
            continue;

          if (pMP2->isBad())
            continue;

          const cv::Mat &d2 = Descriptors2.row(idx2);

          int dist = DescriptorDistance(d1, d2);

          if (dist < bestDist1) {
            bestDist2 = bestDist1;
            bestDist1 = dist;
            bestIdx2 = idx2;
          } else if (dist < bestDist2) {
            bestDist2 = dist;
          }
        }

        if (bestDist1 < TH_LOW) {
          if (static_cast<float>(bestDist1) <mfNNratio * static_cast<float>(bestDist2)) {
            vpMatches12[idx1] = vpMapPoints2[bestIdx2];
            vbMatched2[bestIdx2] = true;

            if (mbCheckOrientation) {
              float rot = vKeysUn1[idx1].angle - vKeysUn2[bestIdx2].angle;
              if (rot < 0.0)
                rot += 360.0f;
              int bin = round(rot * factor);
              if (bin == HISTO_LENGTH)
                bin = 0;
              assert(bin >= 0 && bin < HISTO_LENGTH);
              rotHist[bin].push_back(idx1);
            }
            nmatches++;
          }
        }
      }

      f1it++;
      f2it++;
    } else if (f1it->first < f2it->first) {
      f1it = vFeatVec1.lower_bound(f2it->first);
    } else {
      f2it = vFeatVec2.lower_bound(f1it->first);
    }
  }

  if (mbCheckOrientation) {
    int ind1 = -1;
    int ind2 = -1;
    int ind3 = -1;

    ComputeThreeMaxima(rotHist, HISTO_LENGTH, ind1, ind2, ind3);

    for (int i = 0; i < HISTO_LENGTH; i++) {
      if (i == ind1 || i == ind2 || i == ind3)
        continue;
      for (size_t j = 0, jend = rotHist[i].size(); j < jend; j++) {
        vpMatches12[rotHist[i][j]] = static_cast<MapPoint *>(NULL);
        nmatches--;
      }
    }
  }

  return nmatches;
}

// search by nearest nerghbour
int Associater::SearchByNN(Frame &CurrentFrame, const Frame &LastFrame, const int Ftype) {
  
  std::vector<cv::DMatch> matches;
  cv::BFMatcher desc_matcher(cv::NORM_HAMMING, true);
  desc_matcher.match(LastFrame.Channels[Ftype].mDescriptors, CurrentFrame.Channels[Ftype].mDescriptors, matches, cv::Mat());

  int nmatches = 0;
  for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
    int realIdxKF = matches[i].queryIdx;
    int bestIdxF = matches[i].trainIdx;

    if (matches[i].distance > TH_LOW)
      continue;
    
    MapPoint *pMP = LastFrame.Channels[Ftype].mvpMapPoints[realIdxKF];
    if (!pMP)
      continue;

    if (pMP->isBad())
      continue;

    if (!LastFrame.Channels[Ftype].mvbOutlier[realIdxKF])
      CurrentFrame.Channels[Ftype].mvpMapPoints[bestIdxF] = pMP;

    nmatches++;

  }
  
  return nmatches;
}

// vpMapPointMatches should be the mappoints of frame, we want to match the points on frame to the keyframe
int Associater::SearchByNN(KeyFrame *pKF, Frame &F, std::vector<MapPoint *> &vpMapPointMatches, const int FType) {

  // std::cout << "Matching KeyFrame" << std::endl;
  // std::cout << pKF->mDescriptors.rows << std::endl;
  // std::cout << F.mDescriptors.rows << std::endl;

  const vector<MapPoint *> vpMapPointsKF = pKF->GetMapPointMatches(FType);
  vpMapPointMatches = vector<MapPoint *>(F.Channels[FType].N, static_cast<MapPoint *>(NULL));

  std::vector<cv::DMatch> matches;
  cv::BFMatcher desc_matcher(cv::NORM_HAMMING, true);
  desc_matcher.match(pKF->Channels[FType].mDescriptors, F.Channels[FType].mDescriptors, matches, cv::Mat());

  int nmatches = 0;
  for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
    int realIdxKF = matches[i].queryIdx;
    int bestIdxF = matches[i].trainIdx;

    if (matches[i].distance > TH_HIGH)
      continue;

    MapPoint *pMP = vpMapPointsKF[realIdxKF];

    if (!pMP)
      continue;

    if (pMP->isBad())
      continue;

    vpMapPointMatches[bestIdxF] = pMP;
    nmatches++;
  }
  // std::cout << nmatches << std::endl;

  return nmatches;
}

int Associater::SearchByNN(Frame &F, const vector<MapPoint *> &vpMapPoints) {
  // std::cout << "Matching Localmap" << std::endl;
  // std::cout << vpMapPoints.size() << std::endl;
  // std::cout << F.Channels[0].mDescriptors.rows << std::endl;

  vector<vector<cv::Mat>> MPdescriptorAll;
  vector<vector<int>> select_indice;
  MPdescriptorAll.resize(F.Ntype);
  select_indice.resize(F.Ntype);
  for (size_t iMP = 0; iMP < vpMapPoints.size(); iMP++) {
    MapPoint *pMP = vpMapPoints[iMP];

    if (!pMP)
      continue;

    if (!pMP->mbTrackInView)
      continue;

    if (pMP->isBad())
      continue;
    
    int Ftype = pMP->GetFeatureType();
    if (Ftype == -1)
      continue;

    const cv::Mat MPdescriptor = pMP->GetDescriptor();
    MPdescriptorAll[Ftype].push_back(MPdescriptor);
    select_indice[Ftype].push_back(iMP);
  }

  vector<cv::Mat> MPdescriptors;
  MPdescriptors.resize(F.Ntype);
  for (int Ftype = 0; Ftype < F.Ntype; Ftype++) {
    MPdescriptors[Ftype].create(MPdescriptorAll[Ftype].size(), 32, CV_8U);
  }
  
  for (int Ftype = 0; Ftype < F.Ntype; Ftype++) {
    for (int i = 0; i < static_cast<int>(MPdescriptorAll[Ftype].size()); i++) {
      for (int j = 0; j < 32; j++) {
        MPdescriptors[Ftype].at<unsigned char>(i, j) = MPdescriptorAll[Ftype][i].at<unsigned char>(j);
      }
    }
  }

  vector<vector<cv::DMatch>> matches;
  matches.resize(F.Ntype);
  cv::BFMatcher desc_matcher(cv::NORM_HAMMING, true);
  for (int Ftype = 0; Ftype < F.Ntype; Ftype++) {
    desc_matcher.match(MPdescriptors[Ftype], F.Channels[Ftype].mDescriptors, matches[Ftype], cv::Mat());
  }
  
  int nmatches = 0;
  for (int Ftype = 0; Ftype < F.Ntype; Ftype++) {
    for (int i = 0; i < static_cast<int>(matches[Ftype].size()); ++i) {
      int realIdxMap = select_indice[Ftype][matches[Ftype][i].queryIdx];
      int bestIdxF = matches[Ftype][i].trainIdx;

      if (matches[Ftype][i].distance > TH_HIGH)
        continue;

      if (F.Channels[Ftype].mvpMapPoints[bestIdxF])
        if (F.Channels[Ftype].mvpMapPoints[bestIdxF]->Observations() > 0)
          continue;

      MapPoint *pMP = vpMapPoints[realIdxMap];
      F.Channels[Ftype].mvpMapPoints[bestIdxF] = pMP;

      nmatches++;
    }
  }
  
  // std::cout << MPdescriptors[0].rows << std::endl;
  // std::cout << nmatches << std::endl;

  return nmatches;
}

int Associater::SearchForTriangulation(KeyFrame *pKF1, KeyFrame *pKF2, cv::Mat F12, std::vector<std::pair<size_t, size_t>> &vMatchedPairs, 
                                       const bool bOnlyStereo, const int Ftype) {

  const DBoW2::FeatureVector &vFeatVec1 = pKF1->Channels[Ftype].mFeatVec;
  const DBoW2::FeatureVector &vFeatVec2 = pKF2->Channels[Ftype].mFeatVec;

  // Compute epipole in second image
  cv::Mat Cw = pKF1->GetCameraCenter();
  cv::Mat R2w = pKF2->GetRotation();
  cv::Mat t2w = pKF2->GetTranslation();
  cv::Mat C2 = R2w * Cw + t2w;
  const float invz = 1.0f / C2.at<float>(2);
  const float ex = pKF2->fx * C2.at<float>(0) * invz + pKF2->cx;
  const float ey = pKF2->fy * C2.at<float>(1) * invz + pKF2->cy;

  // Find matches between not tracked keypoints, Matching speed-up by ORB Vocabulary, Compare only ORB that share the same node

  int nmatches = 0;
  vector<bool> vbMatched2(pKF2->Channels[Ftype].N, false);
  vector<int> vMatches12(pKF1->Channels[Ftype].N, -1);

  vector<int> rotHist[HISTO_LENGTH];
  for (int i = 0; i < HISTO_LENGTH; i++)
    rotHist[i].reserve(500);

  const float factor = 1.0f / HISTO_LENGTH;

  DBoW2::FeatureVector::const_iterator f1it = vFeatVec1.begin();
  DBoW2::FeatureVector::const_iterator f2it = vFeatVec2.begin();
  DBoW2::FeatureVector::const_iterator f1end = vFeatVec1.end();
  DBoW2::FeatureVector::const_iterator f2end = vFeatVec2.end();

  while (f1it != f1end && f2it != f2end) {
    if (f1it->first == f2it->first) {
      for (size_t i1 = 0, iend1 = f1it->second.size(); i1 < iend1; i1++) {
        const size_t idx1 = f1it->second[i1];

        MapPoint *pMP1 = pKF1->GetMapPoint(idx1, Ftype);

        // If there is already a MapPoint skip
        if (pMP1)
          continue;

        const bool bStereo1 = pKF1->Channels[Ftype].mvuRight[idx1] >= 0;

        if (bOnlyStereo)
          if (!bStereo1)
            continue;

        const cv::KeyPoint &kp1 = pKF1->Channels[Ftype].mvKeysUn[idx1];

        const cv::Mat &d1 = pKF1->Channels[Ftype].mDescriptors.row(idx1);

        int bestDist = TH_LOW;
        int bestIdx2 = -1;

        for (size_t i2 = 0, iend2 = f2it->second.size(); i2 < iend2; i2++) {
          size_t idx2 = f2it->second[i2];

          MapPoint *pMP2 = pKF2->GetMapPoint(idx2, Ftype);

          // If we have already matched or there is a MapPoint skip
          if (vbMatched2[idx2] || pMP2)
            continue;

          const bool bStereo2 = pKF2->Channels[Ftype].mvuRight[idx2] >= 0;

          if (bOnlyStereo)
            if (!bStereo2)
              continue;

          const cv::Mat &d2 = pKF2->Channels[Ftype].mDescriptors.row(idx2);

          const int dist = DescriptorDistance(d1, d2);

          if (dist > TH_LOW || dist > bestDist)
            continue;

          const cv::KeyPoint &kp2 = pKF2->Channels[Ftype].mvKeysUn[idx2];

          if (!bStereo1 && !bStereo2) {
            const float distex = ex - kp2.pt.x;
            const float distey = ey - kp2.pt.y;
            if (distex * distex + distey * distey < 100 * pKF2->mvScaleFactors[kp2.octave])
              continue;
          }

          if (CheckDistEpipolarLine(kp1, kp2, F12, pKF2)) {
            bestIdx2 = idx2;
            bestDist = dist;
          }
        }

        if (bestIdx2 >= 0) {
          const cv::KeyPoint &kp2 = pKF2->Channels[Ftype].mvKeysUn[bestIdx2];
          vMatches12[idx1] = bestIdx2;
          nmatches++;

          if (mbCheckOrientation) {
            float rot = kp1.angle - kp2.angle;
            if (rot < 0.0)
              rot += 360.0f;
            int bin = round(rot * factor);
            if (bin == HISTO_LENGTH)
              bin = 0;
            assert(bin >= 0 && bin < HISTO_LENGTH);
            rotHist[bin].push_back(idx1);
          }
        }
      }

      f1it++;
      f2it++;
    } else if (f1it->first < f2it->first) {
      f1it = vFeatVec1.lower_bound(f2it->first);
    } else {
      f2it = vFeatVec2.lower_bound(f1it->first);
    }
  }

  if (mbCheckOrientation) {
    int ind1 = -1;
    int ind2 = -1;
    int ind3 = -1;

    ComputeThreeMaxima(rotHist, HISTO_LENGTH, ind1, ind2, ind3);

    for (int i = 0; i < HISTO_LENGTH; i++) {
      if (i == ind1 || i == ind2 || i == ind3)
        continue;
      for (size_t j = 0, jend = rotHist[i].size(); j < jend; j++) {
        vMatches12[rotHist[i][j]] = -1;
        nmatches--;
      }
    }
  }

  vMatchedPairs.clear();
  vMatchedPairs.reserve(nmatches);

  for (size_t i = 0, iend = vMatches12.size(); i < iend; i++) {
    if (vMatches12[i] < 0)
      continue;
    vMatchedPairs.push_back(make_pair(i, vMatches12[i]));
  }

  return nmatches;         
}

int Associater::SearchBySim3(KeyFrame *pKF1, KeyFrame *pKF2, vector<MapPoint *> &vpMatches12, const float &s12,
                             const cv::Mat &R12, const cv::Mat &t12, const float th, const int Ftype) {
  const float &fx = pKF1->fx;
  const float &fy = pKF1->fy;
  const float &cx = pKF1->cx;
  const float &cy = pKF1->cy;

  // Camera 1 from world
  cv::Mat R1w = pKF1->GetRotation();
  cv::Mat t1w = pKF1->GetTranslation();

  // Camera 2 from world
  cv::Mat R2w = pKF2->GetRotation();
  cv::Mat t2w = pKF2->GetTranslation();

  // Transformation between cameras
  cv::Mat sR12 = s12 * R12;
  cv::Mat sR21 = (1.0 / s12) * R12.t();
  cv::Mat t21 = -sR21 * t12;

  const vector<MapPoint *> vpMapPoints1 = pKF1->GetMapPointMatches(Ftype);
  const int N1 = vpMapPoints1.size();

  const vector<MapPoint *> vpMapPoints2 = pKF2->GetMapPointMatches(Ftype);
  const int N2 = vpMapPoints2.size();

  vector<bool> vbAlreadyMatched1(N1, false);
  vector<bool> vbAlreadyMatched2(N2, false);

  for (int i = 0; i < N1; i++) {
    MapPoint *pMP = vpMatches12[i];
    if (pMP) {
      vbAlreadyMatched1[i] = true;
      int idx2 = pMP->GetIndexInKeyFrame(pKF2);
      if (idx2 >= 0 && idx2 < N2)
        vbAlreadyMatched2[idx2] = true;
    }
  }

  vector<int> vnMatch1(N1, -1);
  vector<int> vnMatch2(N2, -1);

  // Transform from KF1 to KF2 and search
  for (int i1 = 0; i1 < N1; i1++) {
    MapPoint *pMP = vpMapPoints1[i1];

    if (!pMP || vbAlreadyMatched1[i1])
      continue;

    if (pMP->isBad())
      continue;

    cv::Mat p3Dw = pMP->GetWorldPos();
    cv::Mat p3Dc1 = R1w * p3Dw + t1w;
    cv::Mat p3Dc2 = sR21 * p3Dc1 + t21;

    // Depth must be positive
    if (p3Dc2.at<float>(2) < 0.0)
      continue;

    const float invz = 1.0 / p3Dc2.at<float>(2);
    const float x = p3Dc2.at<float>(0) * invz;
    const float y = p3Dc2.at<float>(1) * invz;

    const float u = fx * x + cx;
    const float v = fy * y + cy;

    // Point must be inside the image
    if (!pKF2->IsInImage(u, v))
      continue;

    const float maxDistance = pMP->GetMaxDistanceInvariance();
    const float minDistance = pMP->GetMinDistanceInvariance();
    const float dist3D = cv::norm(p3Dc2);

    // Depth must be inside the scale invariance region
    if (dist3D < minDistance || dist3D > maxDistance)
      continue;

    // Compute predicted octave
    const int nPredictedLevel = pMP->PredictScale(dist3D, pKF2);

    // Search in a radius
    const float radius = th * pKF2->mvScaleFactors[nPredictedLevel];

    const vector<size_t> vIndices = pKF2->GetFeaturesInArea(u, v, radius, Ftype);

    if (vIndices.empty())
      continue;

    // Match to the most similar keypoint in the radius
    const cv::Mat dMP = pMP->GetDescriptor();

    int bestDist = INT_MAX;
    int bestIdx = -1;
    for (vector<size_t>::const_iterator vit = vIndices.begin(), vend = vIndices.end(); vit != vend; vit++) {
      const size_t idx = *vit;

      const cv::KeyPoint &kp = pKF2->Channels[Ftype].mvKeysUn[idx];

      if (kp.octave < nPredictedLevel - 1 || kp.octave > nPredictedLevel)
        continue;

      const cv::Mat &dKF = pKF2->Channels[Ftype].mDescriptors.row(idx);

      const int dist = DescriptorDistance(dMP, dKF);

      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = idx;
      }
    }

    if (bestDist <= TH_HIGH) {
      vnMatch1[i1] = bestIdx;
    }
  }

  // Transform from KF2 to KF2 and search
  for (int i2 = 0; i2 < N2; i2++) {
    MapPoint *pMP = vpMapPoints2[i2];

    if (!pMP || vbAlreadyMatched2[i2])
      continue;

    if (pMP->isBad())
      continue;

    cv::Mat p3Dw = pMP->GetWorldPos();
    cv::Mat p3Dc2 = R2w * p3Dw + t2w;
    cv::Mat p3Dc1 = sR12 * p3Dc2 + t12;

    // Depth must be positive
    if (p3Dc1.at<float>(2) < 0.0)
      continue;

    const float invz = 1.0 / p3Dc1.at<float>(2);
    const float x = p3Dc1.at<float>(0) * invz;
    const float y = p3Dc1.at<float>(1) * invz;

    const float u = fx * x + cx;
    const float v = fy * y + cy;

    // Point must be inside the image
    if (!pKF1->IsInImage(u, v))
      continue;

    const float maxDistance = pMP->GetMaxDistanceInvariance();
    const float minDistance = pMP->GetMinDistanceInvariance();
    const float dist3D = cv::norm(p3Dc1);

    // Depth must be inside the scale pyramid of the image
    if (dist3D < minDistance || dist3D > maxDistance)
      continue;

    // Compute predicted octave
    const int nPredictedLevel = pMP->PredictScale(dist3D, pKF1);

    // Search in a radius of 2.5*sigma(ScaleLevel)
    const float radius = th * pKF1->mvScaleFactors[nPredictedLevel];

    const vector<size_t> vIndices = pKF1->GetFeaturesInArea(u, v, radius, Ftype);

    if (vIndices.empty())
      continue;

    // Match to the most similar keypoint in the radius
    const cv::Mat dMP = pMP->GetDescriptor();

    int bestDist = INT_MAX;
    int bestIdx = -1;
    for (vector<size_t>::const_iterator vit = vIndices.begin(), vend = vIndices.end(); vit != vend; vit++) {
      const size_t idx = *vit;

      const cv::KeyPoint &kp = pKF1->Channels[Ftype].mvKeysUn[idx];

      if (kp.octave < nPredictedLevel - 1 || kp.octave > nPredictedLevel)
        continue;

      const cv::Mat &dKF = pKF1->Channels[Ftype].mDescriptors.row(idx);

      const int dist = DescriptorDistance(dMP, dKF);

      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = idx;
      }
    }

    if (bestDist <= TH_HIGH) {
      vnMatch2[i2] = bestIdx;
    }
  }

  // Check agreement
  int nFound = 0;

  for (int i1 = 0; i1 < N1; i1++) {
    int idx2 = vnMatch1[i1];

    if (idx2 >= 0) {
      int idx1 = vnMatch2[idx2];
      if (idx1 == i1) {
        vpMatches12[i1] = vpMapPoints2[idx2];
        nFound++;
      }
    }
  }

  return nFound;
}

int Associater::SearchForInitialization(const int Ftype, Frame &F1, Frame &F2, vector<cv::Point2f> &vbPrevMatched, vector<int> &vnMatches12, int windowSize) {
  int nmatches = 0;
  vnMatches12 = vector<int>(F1.Channels[Ftype].mvKeysUn.size(), -1);

  vector<int> rotHist[HISTO_LENGTH];
  for (int i = 0; i < HISTO_LENGTH; i++)
    rotHist[i].reserve(500);
  const float factor = 1.0f / HISTO_LENGTH;

  vector<int> vMatchedDistance(F2.Channels[Ftype].mvKeysUn.size(), INT_MAX);
  vector<int> vnMatches21(F2.Channels[Ftype].mvKeysUn.size(), -1);

  for (size_t i1 = 0, iend1 = F1.Channels[Ftype].mvKeysUn.size(); i1 < iend1; i1++) {
    cv::KeyPoint kp1 = F1.Channels[Ftype].mvKeysUn[i1];
    int level1 = kp1.octave;
    if (level1 > 0)
      continue;

    vector<size_t> vIndices2 = F2.GetFeaturesInArea(Ftype, vbPrevMatched[i1].x, vbPrevMatched[i1].y, windowSize, level1, level1);

    if (vIndices2.empty())
      continue;

    cv::Mat d1 = F1.Channels[Ftype].mDescriptors.row(i1);

    int bestDist = INT_MAX;
    int bestDist2 = INT_MAX;
    int bestIdx2 = -1;

    for (vector<size_t>::iterator vit = vIndices2.begin(); vit != vIndices2.end(); vit++) {
      size_t i2 = *vit;

      cv::Mat d2 = F2.Channels[Ftype].mDescriptors.row(i2);

      int dist = DescriptorDistance(d1, d2);

      if (vMatchedDistance[i2] <= dist)
        continue;

      if (dist < bestDist) {
        bestDist2 = bestDist;
        bestDist = dist;
        bestIdx2 = i2;
      } else if (dist < bestDist2) {
        bestDist2 = dist;
      }
    }

    if (bestDist <= TH_LOW) {
      if (bestDist < (float)bestDist2 * mfNNratio) {
        if (vnMatches21[bestIdx2] >= 0) {
          vnMatches12[vnMatches21[bestIdx2]] = -1;
          nmatches--;
        }
        vnMatches12[i1] = bestIdx2;
        vnMatches21[bestIdx2] = i1;
        vMatchedDistance[bestIdx2] = bestDist;
        nmatches++;

        if (mbCheckOrientation) {
          float rot = F1.Channels[Ftype].mvKeysUn[i1].angle - F2.Channels[Ftype].mvKeysUn[bestIdx2].angle;
          if (rot < 0.0)
            rot += 360.0f;
          int bin = round(rot * factor);
          if (bin == HISTO_LENGTH)
            bin = 0;
          assert(bin >= 0 && bin < HISTO_LENGTH);
          rotHist[bin].push_back(i1);
        }
      }
    }
  }

  if (mbCheckOrientation) {
    int ind1 = -1;
    int ind2 = -1;
    int ind3 = -1;

    ComputeThreeMaxima(rotHist, HISTO_LENGTH, ind1, ind2, ind3);

    for (int i = 0; i < HISTO_LENGTH; i++) {
      if (i == ind1 || i == ind2 || i == ind3)
        continue;
      for (size_t j = 0, jend = rotHist[i].size(); j < jend; j++) {
        int idx1 = rotHist[i][j];
        if (vnMatches12[idx1] >= 0) {
          vnMatches12[idx1] = -1;
          nmatches--;
        }
      }
    }
  }

  // Update prev matched
  for (size_t i1 = 0, iend1 = vnMatches12.size(); i1 < iend1; i1++)
    if (vnMatches12[i1] >= 0)
      vbPrevMatched[i1] = F2.Channels[Ftype].mvKeysUn[vnMatches12[i1]].pt;

  return nmatches;
}

int Associater::Fuse(const int Ftype, KeyFrame *pKF, const vector<MapPoint *> &vpMapPoints, const float th) {
  cv::Mat Rcw = pKF->GetRotation();
  cv::Mat tcw = pKF->GetTranslation();

  const float &fx = pKF->fx;
  const float &fy = pKF->fy;
  const float &cx = pKF->cx;
  const float &cy = pKF->cy;
  const float &bf = pKF->mbf;

  cv::Mat Ow = pKF->GetCameraCenter();

  int nFused = 0;

  const int nMPs = vpMapPoints.size();

  for (int i = 0; i < nMPs; i++) {
    MapPoint *pMP = vpMapPoints[i];

    if (!pMP)
      continue;

    if (pMP->isBad() || pMP->IsInKeyFrame(pKF))
      continue;

    cv::Mat p3Dw = pMP->GetWorldPos();
    cv::Mat p3Dc = Rcw * p3Dw + tcw;

    // Depth must be positive
    if (p3Dc.at<float>(2) < 0.0f)
      continue;

    const float invz = 1 / p3Dc.at<float>(2);
    const float x = p3Dc.at<float>(0) * invz;
    const float y = p3Dc.at<float>(1) * invz;

    const float u = fx * x + cx;
    const float v = fy * y + cy;

    // Point must be inside the image
    if (!pKF->IsInImage(u, v))
      continue;

    const float ur = u - bf * invz;

    const float maxDistance = pMP->GetMaxDistanceInvariance();
    const float minDistance = pMP->GetMinDistanceInvariance();
    cv::Mat PO = p3Dw - Ow;
    const float dist3D = cv::norm(PO);

    // Depth must be inside the scale pyramid of the image
    if (dist3D < minDistance || dist3D > maxDistance)
      continue;

    // Viewing angle must be less than 60 deg
    cv::Mat Pn = pMP->GetNormal();

    if (PO.dot(Pn) < 0.5 * dist3D)
      continue;

    int nPredictedLevel = pMP->PredictScale(dist3D, pKF);

    // Search in a radius
    const float radius = th * pKF->mvScaleFactors[nPredictedLevel];

    const vector<size_t> vIndices = pKF->GetFeaturesInArea(u, v, radius, Ftype);

    if (vIndices.empty())
      continue;

    // Match to the most similar keypoint in the radius

    const cv::Mat dMP = pMP->GetDescriptor();

    int bestDist = 256;
    int bestIdx = -1;
    for (vector<size_t>::const_iterator vit = vIndices.begin(), vend = vIndices.end(); vit != vend; vit++) {
      const size_t idx = *vit;

      const cv::KeyPoint &kp = pKF->Channels[Ftype].mvKeysUn[idx];

      const int &kpLevel = kp.octave;

      if (kpLevel < nPredictedLevel - 1 || kpLevel > nPredictedLevel)
        continue;

      if (pKF->Channels[Ftype].mvuRight[idx] >= 0) {
        // Check reprojection error in stereo
        const float &kpx = kp.pt.x;
        const float &kpy = kp.pt.y;
        const float &kpr = pKF->Channels[Ftype].mvuRight[idx];
        const float ex = u - kpx;
        const float ey = v - kpy;
        const float er = ur - kpr;
        const float e2 = ex * ex + ey * ey + er * er;

        if (e2 * pKF->mvInvLevelSigma2[kpLevel] > 7.8)
          continue;
      } else {
        const float &kpx = kp.pt.x;
        const float &kpy = kp.pt.y;
        const float ex = u - kpx;
        const float ey = v - kpy;
        const float e2 = ex * ex + ey * ey;

        if (e2 * pKF->mvInvLevelSigma2[kpLevel] > 5.99)
          continue;
      }

      const cv::Mat &dKF = pKF->Channels[Ftype].mDescriptors.row(idx);

      const int dist = DescriptorDistance(dMP, dKF);

      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = idx;
      }
    }

    // If there is already a MapPoint replace otherwise add new measurement
    if (bestDist <= TH_LOW) {
      MapPoint *pMPinKF = pKF->GetMapPoint(bestIdx, Ftype);
      if (pMPinKF) {
        if (!pMPinKF->isBad()) {
          if (pMPinKF->Observations() > pMP->Observations())
            pMP->Replace(pMPinKF);
          else
            pMPinKF->Replace(pMP);
        }
      } else {
        pMP->AddObservation(pKF, bestIdx);
        pKF->AddMapPoint(pMP, bestIdx, Ftype);
      }
      nFused++;
    }
  }

  return nFused;
}

int Associater::Fuse(const int Ftype, KeyFrame *pKF, cv::Mat Scw, const vector<MapPoint *> &vpPoints, float th, vector<MapPoint *> &vpReplacePoint) {
  // Get Calibration Parameters for later projection
  const float &fx = pKF->fx;
  const float &fy = pKF->fy;
  const float &cx = pKF->cx;
  const float &cy = pKF->cy;

  // Decompose Scw
  cv::Mat sRcw = Scw.rowRange(0, 3).colRange(0, 3);
  const float scw = sqrt(sRcw.row(0).dot(sRcw.row(0)));
  cv::Mat Rcw = sRcw / scw;
  cv::Mat tcw = Scw.rowRange(0, 3).col(3) / scw;
  cv::Mat Ow = -Rcw.t() * tcw;

  // Set of MapPoints already found in the KeyFrame
  const set<MapPoint *> spAlreadyFound = pKF->GetMapPoints(Ftype);

  int nFused = 0;

  const int nPoints = vpPoints.size();

  // For each candidate MapPoint project and match
  for (int iMP = 0; iMP < nPoints; iMP++) {
    MapPoint *pMP = vpPoints[iMP];

    // Discard Bad MapPoints and already found
    if (pMP->isBad() || spAlreadyFound.count(pMP))
      continue;

    // Get 3D Coords.
    cv::Mat p3Dw = pMP->GetWorldPos();

    // Transform into Camera Coords.
    cv::Mat p3Dc = Rcw * p3Dw + tcw;

    // Depth must be positive
    if (p3Dc.at<float>(2) < 0.0f)
      continue;

    // Project into Image
    const float invz = 1.0 / p3Dc.at<float>(2);
    const float x = p3Dc.at<float>(0) * invz;
    const float y = p3Dc.at<float>(1) * invz;

    const float u = fx * x + cx;
    const float v = fy * y + cy;

    // Point must be inside the image
    if (!pKF->IsInImage(u, v))
      continue;

    // Depth must be inside the scale pyramid of the image
    const float maxDistance = pMP->GetMaxDistanceInvariance();
    const float minDistance = pMP->GetMinDistanceInvariance();
    cv::Mat PO = p3Dw - Ow;
    const float dist3D = cv::norm(PO);

    if (dist3D < minDistance || dist3D > maxDistance)
      continue;

    // Viewing angle must be less than 60 deg
    cv::Mat Pn = pMP->GetNormal();

    if (PO.dot(Pn) < 0.5 * dist3D)
      continue;

    // Compute predicted scale level
    const int nPredictedLevel = pMP->PredictScale(dist3D, pKF);

    // Search in a radius
    const float radius = th * pKF->mvScaleFactors[nPredictedLevel];

    const vector<size_t> vIndices = pKF->GetFeaturesInArea(u, v, radius, Ftype);

    if (vIndices.empty())
      continue;

    // Match to the most similar keypoint in the radius

    const cv::Mat dMP = pMP->GetDescriptor();

    int bestDist = INT_MAX;
    int bestIdx = -1;
    for (vector<size_t>::const_iterator vit = vIndices.begin(); vit != vIndices.end(); vit++) {
      const size_t idx = *vit;
      const int &kpLevel = pKF->Channels[Ftype].mvKeysUn[idx].octave;

      if (kpLevel < nPredictedLevel - 1 || kpLevel > nPredictedLevel)
        continue;

      const cv::Mat &dKF = pKF->Channels[Ftype].mDescriptors.row(idx);

      int dist = DescriptorDistance(dMP, dKF);

      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = idx;
      }
    }

    // If there is already a MapPoint replace otherwise add new measurement
    if (bestDist <= TH_LOW) {
      MapPoint *pMPinKF = pKF->GetMapPoint(bestIdx, Ftype);
      if (pMPinKF) {
        if (!pMPinKF->isBad())
          vpReplacePoint[iMP] = pMPinKF;
      } else {
        pMP->AddObservation(pKF, bestIdx);
        pKF->AddMapPoint(pMP, bestIdx, Ftype);
      }
      nFused++;
    }
  }

  return nFused;
}

// compute three maxima
void Associater::ComputeThreeMaxima(vector<int> *histo, const int L, int &ind1, int &ind2, int &ind3) {
  int max1 = 0;
  int max2 = 0;
  int max3 = 0;

  for (int i = 0; i < L; i++) {
    const int s = histo[i].size();
    if (s > max1) {
      max3 = max2;
      max2 = max1;
      max1 = s;
      ind3 = ind2;
      ind2 = ind1;
      ind1 = i;
    } else if (s > max2) {
      max3 = max2;
      max2 = s;
      ind3 = ind2;
      ind2 = i;
    } else if (s > max3) {
      max3 = s;
      ind3 = i;
    }
  }

  if (max2 < 0.1f * (float)max1) {
    ind2 = -1;
    ind3 = -1;
  } else if (max3 < 0.1f * (float)max1) {
    ind3 = -1;
  }
}

int Associater::DescriptorDistance(const cv::Mat &a, const cv::Mat &b) {

  const uchar* pa = a.ptr<uchar>();
  const uchar* pb = b.ptr<uchar>();

  int dist = 0;
  for (int i = 0; i < a.cols; ++i, ++pa, ++pb)
    dist += __builtin_popcount(*pa ^ *pb);


  dist = int(dist * 32.0f / a.cols + 0.5f); // Normalise to 32B standard

  /*
  const int *pa = a.ptr<int32_t>();
  const int *pb = b.ptr<int32_t>();

  int dist = 0;

  for (int i = 0; i < 8; i++, pa++, pb++) {
    unsigned int v = *pa ^ *pb;
    v = v - ((v >> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    dist += (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
  }
  */

  return dist;
}

float Associater::RadiusByViewingCos(const float &viewCos) {
  if (viewCos > 0.998)
    return 2.5;
  else
    return 4.0;
}

bool Associater::CheckDistEpipolarLine(const cv::KeyPoint &kp1, const cv::KeyPoint &kp2, const cv::Mat &F12, const KeyFrame *pKF2) {
  // Epipolar line in second image l = x1'F12 = [a b c]
  const float a = kp1.pt.x * F12.at<float>(0, 0) + kp1.pt.y * F12.at<float>(1, 0) + F12.at<float>(2, 0);
  const float b = kp1.pt.x * F12.at<float>(0, 1) + kp1.pt.y * F12.at<float>(1, 1) + F12.at<float>(2, 1);
  const float c = kp1.pt.x * F12.at<float>(0, 2) + kp1.pt.y * F12.at<float>(1, 2) + F12.at<float>(2, 2);

  const float num = a * kp2.pt.x + b * kp2.pt.y + c;

  const float den = a * a + b * b;

  if (den == 0)
    return false;

  const float dsqr = num * num / den;

  return dsqr < 3.84 * pKF2->mvLevelSigma2[kp2.octave];
}

}