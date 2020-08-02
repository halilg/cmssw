#include <cmath>

#include "CommonTools/PileupAlgos/interface/PuppiContainer.h"
#include "DataFormats/Math/interface/deltaR.h"
#include "Math/ProbFunc.h"
#include "TMath.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/isFinite.h"

PuppiContainer::PuppiContainer(const edm::ParameterSet &iConfig) {
  fPuppiDiagnostics = iConfig.getParameter<bool>("puppiDiagnostics");
  fApplyCHS = iConfig.getParameter<bool>("applyCHS");
  fInvert = iConfig.getParameter<bool>("invertPuppi");
  fUseExp = iConfig.getParameter<bool>("useExp");
  fPuppiWeightCut = iConfig.getParameter<double>("MinPuppiWeight");
  fPtMaxPhotons = iConfig.getParameter<double>("PtMaxPhotons");
  fEtaMaxPhotons = iConfig.getParameter<double>("EtaMaxPhotons");
  fPtMaxNeutrals = iConfig.getParameter<double>("PtMaxNeutrals");
  fPtMaxNeutralsStartSlope = iConfig.getParameter<double>("PtMaxNeutralsStartSlope");
  std::vector<edm::ParameterSet> lAlgos = iConfig.getParameter<std::vector<edm::ParameterSet> >("algos");
  fNAlgos = lAlgos.size();
  for (unsigned int i0 = 0; i0 < lAlgos.size(); i0++) {
    PuppiAlgo pPuppiConfig(lAlgos[i0]);
    fPuppiAlgo.push_back(pPuppiConfig);
  }
}

PuppiContainer::~PuppiContainer() {}

void PuppiContainer::initialize(std::vector<PuppiCandidate> const& iPuppiCandidates) {
  //Clear everything
  fPFParticles.clear();
  fChargedPV.clear();
  fWeights.clear();
  fVals.clear();
  fRawAlphas.clear();
  fAlphaMed.clear();
  fAlphaRMS.clear();
  fNPV = 1.;
  for (auto const &pParticle : iPuppiCandidates) {
    fPFParticles.emplace_back(pParticle);
    //charged particles associated to PV
    if (pParticle.id == 1)
      fChargedPV.emplace_back(pParticle);
  }
}

float PuppiContainer::goodVar(PuppiCandidate const &iPart,
                               std::vector<PuppiCandidate> const &iParts,
                               int const iOpt,
                               float const iRCone) const {
  return var_within_R(iOpt, iParts, iPart, iRCone);
}

float PuppiContainer::var_within_R(int const iId, std::vector<PuppiCandidate> const& particles, PuppiCandidate const& centre, float const R) const {
  if (iId == -1)
    return 1;

  //this is a circle in rapidity-phi
  //it would make more sense to have var definition consistent
  //fastjet::Selector sel = fastjet::SelectorCircle(R);
  //sel.set_reference(centre);
  //the original code used Selector infrastructure: it is too heavy here
  //logic of SelectorCircle is preserved below
  float const r2(R * R);
  float var((iId == 1) ? centre.pt : 0.f);
  for (auto const &part : particles) {
    if (part.id == 3)
      continue;
    //squared_distance is in (y,phi) coords: rap() has faster access -> check it first
    if (std::abs(part.eta - centre.eta) < R) {
      auto const dr2(reco::deltaR2(part.eta, part.phi, centre.eta, centre.phi));
      if((dr2 < r2) and (dr2 > 0.0001)){
        auto const pt(part.pt);
        if (iId == 5)
          var += (pt * pt / dr2);
        else if (iId == 4)
          var += pt;
        else if (iId == 3)
          var += (1. / dr2);
        else if (iId == 2)
          var += (1. / dr2);
        else if (iId == 1)
          var += pt;
        else if (iId == 0)
          var += (pt / dr2);
      }
    }
  }

  if((var != 0) and ((iId == 0) or (iId == 3) or (iId == 5)))
    var = log(var);

  return var;
}

//In fact takes the median not the average
void PuppiContainer::getRMSAvg(int iOpt,
                               std::vector<PuppiCandidate> const &iConstits,
                               std::vector<PuppiCandidate> const &iParticles,
                               std::vector<PuppiCandidate> const &iChargedParticles) {
  for (unsigned int i0 = 0; i0 < iConstits.size(); i0++) {
    float pVal = -1;
    //Calculate the Puppi Algo to use
    int pPupId = getPuppiId(iConstits[i0].pt, iConstits[i0].eta);
    if (pPupId == -1 || fPuppiAlgo[pPupId].numAlgos() <= iOpt) {
      fVals.push_back(-1);
      continue;
    }
    //Get the Puppi Sub Algo (given iteration)
    int pAlgo = fPuppiAlgo[pPupId].algoId(iOpt);
    bool pCharged = fPuppiAlgo[pPupId].isCharged(iOpt);
    float pCone = fPuppiAlgo[pPupId].coneSize(iOpt);
    //Compute the Puppi Metric
    if (!pCharged)
      pVal = goodVar(iConstits[i0], iParticles, pAlgo, pCone);
    if (pCharged)
      pVal = goodVar(iConstits[i0], iChargedParticles, pAlgo, pCone);
    fVals.push_back(pVal);

    if (!edm::isFinite(pVal)) {
      LogDebug("NotFound") << "====> Value is Nan " << pVal << " == " << iConstits[i0].pt << " -- "
                           << iConstits[i0].eta;
      continue;
    }

    // // fPuppiAlgo[pPupId].add(iConstits[i0],pVal,iOpt);
    //code added by Nhan, now instead for every algorithm give it all the particles
    for (int i1 = 0; i1 < fNAlgos; i1++) {
      pAlgo = fPuppiAlgo[i1].algoId(iOpt);
      pCharged = fPuppiAlgo[i1].isCharged(iOpt);
      pCone = fPuppiAlgo[i1].coneSize(iOpt);
      float curVal = -1;
      if (i1 != pPupId) {
        if (!pCharged)
          curVal = goodVar(iConstits[i0], iParticles, pAlgo, pCone);
        if (pCharged)
          curVal = goodVar(iConstits[i0], iChargedParticles, pAlgo, pCone);
      } else {  //no need to repeat the computation
        curVal = pVal;
      }

      fPuppiAlgo[i1].add(iConstits[i0], curVal, iOpt);
    }
  }

  for (int i0 = 0; i0 < fNAlgos; i0++)
    fPuppiAlgo[i0].computeMedRMS(iOpt);
}

//In fact takes the median not the average
void PuppiContainer::getRawAlphas(int const iOpt,
                                  std::vector<PuppiCandidate> const &iConstits,
                                  std::vector<PuppiCandidate> const &iParticles,
                                  std::vector<PuppiCandidate> const &iChargedParticles) {
  for (int j0 = 0; j0 < fNAlgos; j0++) {
    for (unsigned int i0 = 0; i0 < iConstits.size(); i0++) {
      float pVal = -1;
      //Get the Puppi Sub Algo (given iteration)
      int pAlgo = fPuppiAlgo[j0].algoId(iOpt);
      bool pCharged = fPuppiAlgo[j0].isCharged(iOpt);
      float pCone = fPuppiAlgo[j0].coneSize(iOpt);
      //Compute the Puppi Metric
      if (!pCharged)
        pVal = goodVar(iConstits[i0], iParticles, pAlgo, pCone);
      if (pCharged)
        pVal = goodVar(iConstits[i0], iChargedParticles, pAlgo, pCone);
      fRawAlphas.push_back(pVal);
      if (!edm::isFinite(pVal)) {
        LogDebug("NotFound") << "====> Value is Nan " << pVal << " == " << iConstits[i0].pt << " -- "
                             << iConstits[i0].eta;
        continue;
      }
    }
  }
}

int PuppiContainer::getPuppiId(float iPt, float iEta) {
  int lId = -1;
  for (int i0 = 0; i0 < fNAlgos; i0++) {
    int nEtaBinsPerAlgo = fPuppiAlgo[i0].etaBins();
    for (int i1 = 0; i1 < nEtaBinsPerAlgo; i1++) {
      if ((std::abs(iEta) > fPuppiAlgo[i0].etaMin(i1)) && (std::abs(iEta) < fPuppiAlgo[i0].etaMax(i1))) {
        fPuppiAlgo[i0].fixAlgoEtaBin(i1);
        if (iPt > fPuppiAlgo[i0].ptMin()) {
          lId = i0;
          break;
        }
      }
    }
  }

  return lId;
}

float PuppiContainer::getChi2FromdZ(float const iDZ) const {
  //We need to obtain prob of PU + (1-Prob of LV)
  // Prob(LV) = Gaus(dZ,sigma) where sigma = 1.5mm  (its really more like 1mm)
  //float lProbLV = ROOT::Math::normal_cdf_c(std::abs(iDZ),0.2)*2.; //*2 is to do it double sided
  //Take iDZ to be corrected by sigma already
  float lProbLV = ROOT::Math::normal_cdf_c(std::abs(iDZ), 1.) * 2.;  //*2 is to do it double sided
  float lProbPU = 1 - lProbLV;
  if (lProbPU <= 0)
    lProbPU = 1e-16;  //Quick Trick to through out infs
  if (lProbPU >= 0)
    lProbPU = 1 - 1e-16;  //Ditto
  float lChi2PU = TMath::ChisquareQuantile(lProbPU, 1);
  lChi2PU *= lChi2PU;
  return lChi2PU;
}
std::vector<float> const &PuppiContainer::puppiWeights() {
  int lNParticles = fPFParticles.size();

  fWeights.clear();
  fWeights.reserve(lNParticles);

  for (int i0 = 0; i0 < fNAlgos; i0++)
    fPuppiAlgo[i0].reset();

  int lNMaxAlgo = 1;
  for (int i0 = 0; i0 < fNAlgos; i0++)
    lNMaxAlgo = std::max(fPuppiAlgo[i0].numAlgos(), lNMaxAlgo);

  //Run through all compute mean and RMS
  fVals.clear();
  fVals.reserve(lNParticles * lNMaxAlgo);
  for (int i0 = 0; i0 < lNMaxAlgo; i0++) {
    getRMSAvg(i0, fPFParticles, fPFParticles, fChargedPV);
  }
  if (fPuppiDiagnostics)
    getRawAlphas(0, fPFParticles, fPFParticles, fChargedPV);

  std::vector<float> pVals;
  for (int i0 = 0; i0 < lNParticles; i0++) {
    //Refresh
    pVals.clear();
    float pWeight = 1;
    //Get the Puppi Id and if ill defined move on
    const auto &rParticle = fPFParticles.at(i0);
    int pPupId = getPuppiId(rParticle.pt, rParticle.eta);
    if (pPupId == -1) {
      fWeights.push_back(0);
      fAlphaMed.push_back(-10);
      fAlphaRMS.push_back(-10);
      continue;
    }

    // fill the p-values
    float pChi2 = 0;
    if (fUseExp) {
      //Compute an Experimental Puppi Weight with delta Z info (very simple example)
      pChi2 = getChi2FromdZ(rParticle.dZ);
      //Now make sure Neutrals are not set
      if ((std::abs(rParticle.pdgId) == 22) || (std::abs(rParticle.pdgId) == 130))
        pChi2 = 0;
    }
    //Fill and compute the PuppiWeight
    int lNAlgos = fPuppiAlgo[pPupId].numAlgos();
    pVals.reserve(lNAlgos);
    for (int i1 = 0; i1 < lNAlgos; i1++)
      pVals.push_back(fVals[lNParticles * i1 + i0]);

    pWeight = fPuppiAlgo[pPupId].compute(pVals, pChi2);
    //Apply weight of 1 for leptons if puppiNoLep
    if (rParticle.id == 3)
      pWeight = 1;
    //Apply the CHS weights
    else if (rParticle.id == 1 && fApplyCHS)
      pWeight = 1;
    else if (rParticle.id == 2 && fApplyCHS)
      pWeight = 0;
    //Basic Weight Checks
    if (!edm::isFinite(pWeight)) {
      pWeight = 0.0;
      LogDebug("PuppiWeightError") << "====> Weight is nan : " << pWeight << " : pt " << rParticle.pt
                                   << " -- eta : " << rParticle.eta << " -- Value" << fVals[i0]
                                   << " -- id :  " << rParticle.id << " --  NAlgos: " << lNAlgos;
    }
    //Basic Cuts
    if (pWeight * fPFParticles[i0].pt < fPuppiAlgo[pPupId].neutralPt(fNPV) && rParticle.id == 0)
      pWeight = 0;  //threshold cut on the neutral Pt
    // Protect high pT photons (important for gamma to hadronic recoil balance)
    if ((fPtMaxPhotons > 0) && (rParticle.pdgId == 22) && (std::abs(fPFParticles[i0].eta) < fEtaMaxPhotons) &&
        (fPFParticles[i0].pt > fPtMaxPhotons))
      pWeight = 1.;
    // Protect high pT neutrals
    else if ((fPtMaxNeutrals > 0) && (rParticle.id == 0))
      pWeight =
          std::clamp((fPFParticles[i0].pt - fPtMaxNeutralsStartSlope) / (fPtMaxNeutrals - fPtMaxNeutralsStartSlope),
                     pWeight,
                     1.f);
    if (pWeight < fPuppiWeightCut)
      pWeight = 0;  //==> Eliminate the low Weight stuff
    if (fInvert)
      pWeight = 1. - pWeight;

    fWeights.push_back(pWeight);
    fAlphaMed.push_back(fPuppiAlgo[pPupId].median());
    fAlphaRMS.push_back(fPuppiAlgo[pPupId].rms());
    //Now get rid of the thrown out weights for the particle collection

    // leave these lines in, in case want to move eventually to having no 1-to-1 correspondence between puppi and pf cands
    // if( std::abs(pWeight) < std::numeric_limits<double>::denorm_min() ) continue; // this line seems not to work like it's supposed to...
    // if(std::abs(pWeight) <= 0. ) continue;
  }
  return fWeights;
}
