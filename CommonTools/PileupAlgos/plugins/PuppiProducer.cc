#include <memory>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "DataFormats/Common/interface/ValueMap.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/Common/interface/View.h"
#include "DataFormats/Candidate/interface/Candidate.h"
#include "DataFormats/Candidate/interface/CandidateFwd.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrackFwd.h"
#include "DataFormats/Common/interface/Association.h"

#include "CommonTools/PileupAlgos/plugins/PuppiProducer.h"
#include "CommonTools/PileupAlgos/interface/PuppiCandidate.h"

// ------------------------------------------------------------------------------------------
PuppiProducer::PuppiProducer(const edm::ParameterSet& iConfig) {
  fPuppiDiagnostics = iConfig.getParameter<bool>("puppiDiagnostics");
  fPuppiNoLep = iConfig.getParameter<bool>("puppiNoLep");
  fUseFromPVLooseTight = iConfig.getParameter<bool>("UseFromPVLooseTight");
  fUseDZ = iConfig.getParameter<bool>("UseDeltaZCut");
  fDZCut = iConfig.getParameter<double>("DeltaZCut");
  fEtaMinUseDZ = iConfig.getParameter<double>("EtaMinUseDeltaZ");
  fPtMaxCharged = iConfig.getParameter<double>("PtMaxCharged");
  fEtaMaxCharged = iConfig.getParameter<double>("EtaMaxCharged");
  fPtMaxPhotons = iConfig.getParameter<double>("PtMaxPhotons");
  fEtaMaxPhotons = iConfig.getParameter<double>("EtaMaxPhotons");
  fUseExistingWeights = iConfig.getParameter<bool>("useExistingWeights");
  fClonePackedCands = iConfig.getParameter<bool>("clonePackedCands");
  fVtxNdofCut = iConfig.getParameter<int>("vtxNdofCut");
  fVtxZCut = iConfig.getParameter<double>("vtxZCut");
  fPuppiContainer = std::unique_ptr<PuppiContainer>(new PuppiContainer(iConfig));

  tokenPFCandidates_ = consumes<CandidateView>(iConfig.getParameter<edm::InputTag>("candName"));
  tokenVertices_ = consumes<VertexCollection>(iConfig.getParameter<edm::InputTag>("vertexName"));

  ptokenPupOut_ = produces<edm::ValueMap<float>>();
  ptokenP4PupOut_ = produces<edm::ValueMap<LorentzVector>>();
  ptokenValues_ = produces<edm::ValueMap<reco::CandidatePtr>>();

  if (fUseExistingWeights || fClonePackedCands)
    ptokenPackedPuppiCandidates_ = produces<pat::PackedCandidateCollection>();
  else {
    ptokenPuppiCandidates_ = produces<reco::PFCandidateCollection>();
  }

  if (fPuppiDiagnostics) {
    ptokenNalgos_ = produces<float>("PuppiNAlgos");
    ptokenRawAlphas_ = produces<std::vector<float>>("PuppiRawAlphas");
    ptokenAlphas_ = produces<std::vector<float>>("PuppiAlphas");
    ptokenAlphasMed_ = produces<std::vector<float>>("PuppiAlphasMed");
    ptokenAlphasRms_ = produces<std::vector<float>>("PuppiAlphasRms");
  }
}

PuppiProducer::~PuppiProducer() {}

void PuppiProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  // Get PFCandidate Collection
  edm::Handle<CandidateView> hPFProduct;
  iEvent.getByToken(tokenPFCandidates_, hPFProduct);
  const CandidateView* pfCol = hPFProduct.product();

  // Get vertex collection w/PV as the first entry?
  edm::Handle<reco::VertexCollection> hVertexProduct;
  iEvent.getByToken(tokenVertices_, hVertexProduct);
  const reco::VertexCollection* pvCol = hVertexProduct.product();

  int npv = 0;
  const reco::VertexCollection::const_iterator vtxEnd = pvCol->end();
  for (reco::VertexCollection::const_iterator vtxIter = pvCol->begin(); vtxEnd != vtxIter; ++vtxIter) {
    if (!vtxIter->isFake() && vtxIter->ndof() >= fVtxNdofCut && std::abs(vtxIter->z()) <= fVtxZCut)
      npv++;
  }

  std::vector<float> lWeights;
  if (!fUseExistingWeights) {
    std::vector<PuppiCandidate> fPuppiCandCollection;
    fPuppiCandCollection.reserve(pfCol->size());
    for (auto const& aPF : *pfCol) {
      PuppiCandidate puppiCand;
      puppiCand.pt = aPF.pt();
      puppiCand.eta = aPF.eta();
      puppiCand.phi = aPF.phi();
      puppiCand.mass = aPF.mass();
      puppiCand.charge = aPF.charge();
      puppiCand.pdgId = aPF.pdgId();
      bool const isLepton = ((std::abs(puppiCand.pdgId) == 11) || (std::abs(puppiCand.pdgId) == 13));
      const reco::Vertex* closestVtx = nullptr;
      float pDZ = -9999;
      float pD0 = -9999;
      int pVtxId = -9999;
      const pat::PackedCandidate* lPack = dynamic_cast<const pat::PackedCandidate*>(&aPF);
      if (lPack == nullptr) {
        const reco::PFCandidate* pPF = dynamic_cast<const reco::PFCandidate*>(&aPF);
        float curdz = 9999;
        int closestVtxForUnassociateds = -9999;
        bool lFirst = true;
        const reco::TrackRef aTrackRef = pPF->trackRef();
        for (auto const& aV : *pvCol) {
          if (lFirst) {
            if (aTrackRef.isNonnull()) {
              pDZ = aTrackRef->dz(aV.position());
              pD0 = aTrackRef->d0();
            } else if (pPF->gsfTrackRef().isNonnull()) {
              pDZ = pPF->gsfTrackRef()->dz(aV.position());
              pD0 = pPF->gsfTrackRef()->d0();
            }
            lFirst = false;
            if (pDZ > -9999)
              pVtxId = 0;
          }
          if (aTrackRef.isNonnull() && aV.trackWeight(pPF->trackRef()) > 0) {
            closestVtx = &aV;
            break;
          }
          // in case it's unassocciated, keep more info
          float tmpdz = 99999;
          if (aTrackRef.isNonnull())
            tmpdz = aTrackRef->dz(aV.position());
          else if (pPF->gsfTrackRef().isNonnull())
            tmpdz = pPF->gsfTrackRef()->dz(aV.position());
          if (std::abs(tmpdz) < curdz) {
            curdz = std::abs(tmpdz);
            closestVtxForUnassociateds = pVtxId;
          }
          pVtxId++;
        }
        int tmpFromPV = 0;
        // mocking the miniAOD definitions
        if (std::abs(puppiCand.charge) > 0) {
          if (closestVtx != nullptr && pVtxId > 0)
            tmpFromPV = 0;
          if (closestVtx != nullptr && pVtxId == 0)
            tmpFromPV = 3;
          if (closestVtx == nullptr && closestVtxForUnassociateds == 0)
            tmpFromPV = 2;
          if (closestVtx == nullptr && closestVtxForUnassociateds != 0)
            tmpFromPV = 1;
        }
        puppiCand.dZ = pDZ;
        puppiCand.d0 = pD0;
        puppiCand.id = 0;
        if (std::abs(puppiCand.charge) == 0) {
          puppiCand.id = 0;
        } else {
          if (fPuppiNoLep && isLepton)
            puppiCand.id = 3;
          else if (tmpFromPV == 0) {
            puppiCand.id = 2;
          }  // 0 is associated to PU vertex
          else if (tmpFromPV == 3) {
            puppiCand.id = 1;
          } else if (tmpFromPV == 1 || tmpFromPV == 2) {
            puppiCand.id = 0;
            if ((fPtMaxCharged > 0) and (puppiCand.pt > fPtMaxCharged))
              puppiCand.id = 1;
            else if (std::abs(puppiCand.eta) > fEtaMaxCharged)
              puppiCand.id = 1;
            else if ((fUseDZ) && (std::abs(puppiCand.eta) >= fEtaMinUseDZ))
              puppiCand.id = (std::abs(pDZ) < fDZCut) ? 1 : 2;
            else if (fUseFromPVLooseTight && tmpFromPV == 1)
              puppiCand.id = 2;
            else if (fUseFromPVLooseTight && tmpFromPV == 2)
              puppiCand.id = 1;
          }
        }
      } else if (lPack->vertexRef().isNonnull()) {
        pDZ = lPack->dz();
        pD0 = lPack->dxy();
        puppiCand.dZ = pDZ;
        puppiCand.d0 = pD0;

        puppiCand.id = 0;
        if (std::abs(puppiCand.charge) == 0) {
          puppiCand.id = 0;
        }
        if (std::abs(puppiCand.charge) > 0) {
          if (fPuppiNoLep && isLepton)
            puppiCand.id = 3;
          else if (lPack->fromPV() == 0) {
            puppiCand.id = 2;
          }  // 0 is associated to PU vertex
          else if (lPack->fromPV() == (pat::PackedCandidate::PVUsedInFit)) {
            puppiCand.id = 1;
          } else if (lPack->fromPV() == (pat::PackedCandidate::PVTight) ||
                     lPack->fromPV() == (pat::PackedCandidate::PVLoose)) {
            puppiCand.id = 0;
            if ((fPtMaxCharged > 0) and (puppiCand.pt > fPtMaxCharged))
              puppiCand.id = 1;
            else if (std::abs(puppiCand.eta) > fEtaMaxCharged)
              puppiCand.id = 1;
            else if ((fUseDZ) && (std::abs(puppiCand.eta) >= fEtaMinUseDZ))
              puppiCand.id = (std::abs(pDZ) < fDZCut) ? 1 : 2;
            else if (fUseFromPVLooseTight && lPack->fromPV() == (pat::PackedCandidate::PVLoose))
              puppiCand.id = 2;
            else if (fUseFromPVLooseTight && lPack->fromPV() == (pat::PackedCandidate::PVTight))
              puppiCand.id = 1;
          }
        }
      }

      fPuppiCandCollection.push_back(puppiCand);
    }

    fPuppiContainer->run(fPuppiCandCollection, npv);
    lWeights = fPuppiContainer->puppiWeights();
  } else {
    //Use the existing weights
    int lPackCtr = 0;
    for (auto const& aPF : *pfCol) {
      const pat::PackedCandidate* lPack = dynamic_cast<const pat::PackedCandidate*>(&aPF);
      float curpupweight = -1.;
      if (lPack == nullptr) {
        // throw error
        throw edm::Exception(edm::errors::LogicError,
                             "PuppiProducer: cannot get weights since inputs are not PackedCandidates");
      } else {
        if (fPuppiNoLep) {
          curpupweight = lPack->puppiWeightNoLep();
        } else {
          curpupweight = lPack->puppiWeight();
        }
      }
      // Protect high pT photons (important for gamma to hadronic recoil balance)
      if ((fPtMaxPhotons > 0) && (lPack->pdgId() == 22) && (std::abs(lPack->eta()) < fEtaMaxPhotons) &&
          (lPack->pt() > fPtMaxPhotons))
        curpupweight = 1;
      lWeights.push_back(curpupweight);
      lPackCtr++;
    }
  }

  //Fill it into the event
  edm::ValueMap<float> lPupOut;
  edm::ValueMap<float>::Filler lPupFiller(lPupOut);
  lPupFiller.insert(hPFProduct, lWeights.begin(), lWeights.end());
  lPupFiller.fill();

  // This is a dummy to access the "translate" method which is a
  // non-static member function even though it doesn't need to be.
  // Will fix in the future.
  static const reco::PFCandidate dummySinceTranslateIsNotStatic;

  // Fill a new PF/Packed Candidate Collection and write out the ValueMap of the new p4s.
  // Since the size of the ValueMap must be equal to the input collection, we need
  // to search the "puppi" particles to find a match for each input. If none is found,
  // the input is set to have a four-vector of 0,0,0,0
  PFOutputCollection fPuppiCandidates;
  PackedOutputCollection fPackedPuppiCandidates;

  edm::ValueMap<LorentzVector> p4PupOut;
  LorentzVectorCollection puppiP4s;
  std::vector<reco::CandidatePtr> values(hPFProduct->size());

  int iCand = -1;
  for (auto const& aCand : *hPFProduct) {
    ++iCand;
    std::unique_ptr<pat::PackedCandidate> pCand;
    std::unique_ptr<reco::PFCandidate> pfCand;

    if (fUseExistingWeights || fClonePackedCands) {
      const pat::PackedCandidate* cand = dynamic_cast<const pat::PackedCandidate*>(&aCand);
      if (!cand)
        throw edm::Exception(edm::errors::LogicError, "PuppiProducer: inputs are not PackedCandidates");
      pCand.reset(new pat::PackedCandidate(*cand));
    } else {
      auto id = dummySinceTranslateIsNotStatic.translatePdgIdToType(aCand.pdgId());
      const reco::PFCandidate* cand = dynamic_cast<const reco::PFCandidate*>(&aCand);
      pfCand.reset(new reco::PFCandidate(cand ? *cand : reco::PFCandidate(aCand.charge(), aCand.p4(), id)));
    }

    // Here, we are using new weights computed and putting them in the packed candidates.
    if (fClonePackedCands && (!fUseExistingWeights)) {
      if (fPuppiNoLep)
        pCand->setPuppiWeight(pCand->puppiWeight(), lWeights[iCand]);
      else
        pCand->setPuppiWeight(lWeights[iCand], pCand->puppiWeightNoLep());
    }

    puppiP4s.emplace_back(lWeights[iCand] * aCand.px(),
                          lWeights[iCand] * aCand.py(),
                          lWeights[iCand] * aCand.pz(),
                          lWeights[iCand] * aCand.energy());

    // Here, we are using existing weights, or we're using packed candidates.
    // That is, whether or not we recomputed the weights, we store the
    // source candidate appropriately, and set the p4 of the packed candidate.
    if (fUseExistingWeights || fClonePackedCands) {
      pCand->setP4(puppiP4s.back());
      pCand->setSourceCandidatePtr(aCand.sourceCandidatePtr(0));
      fPackedPuppiCandidates.push_back(*pCand);
    } else {
      pfCand->setP4(puppiP4s.back());
      pfCand->setSourceCandidatePtr(aCand.sourceCandidatePtr(0));
      fPuppiCandidates.push_back(*pfCand);
    }
  }

  //Compute the modified p4s
  edm::ValueMap<LorentzVector>::Filler p4PupFiller(p4PupOut);
  p4PupFiller.insert(hPFProduct, puppiP4s.begin(), puppiP4s.end());
  p4PupFiller.fill();

  iEvent.emplace(ptokenPupOut_, lPupOut);
  iEvent.emplace(ptokenP4PupOut_, p4PupOut);
  if (fUseExistingWeights || fClonePackedCands) {
    edm::OrphanHandle<pat::PackedCandidateCollection> oh =
        iEvent.emplace(ptokenPackedPuppiCandidates_, fPackedPuppiCandidates);
    for (unsigned int ic = 0, nc = oh->size(); ic < nc; ++ic) {
      reco::CandidatePtr pkref(oh, ic);
      values[ic] = pkref;
    }
  } else {
    edm::OrphanHandle<reco::PFCandidateCollection> oh = iEvent.emplace(ptokenPuppiCandidates_, fPuppiCandidates);
    for (unsigned int ic = 0, nc = oh->size(); ic < nc; ++ic) {
      reco::CandidatePtr pkref(oh, ic);
      values[ic] = pkref;
    }
  }
  edm::ValueMap<reco::CandidatePtr> pfMap_p;
  edm::ValueMap<reco::CandidatePtr>::Filler filler(pfMap_p);
  filler.insert(hPFProduct, values.begin(), values.end());
  filler.fill();
  iEvent.emplace(ptokenValues_, pfMap_p);

  //////////////////////////////////////////////
  if (fPuppiDiagnostics && !fUseExistingWeights) {
    // all the different alphas per particle
    // THE alpha per particle
    std::vector<float> theAlphas(fPuppiContainer->puppiAlphas());
    std::vector<float> theAlphasMed(fPuppiContainer->puppiAlphasMed());
    std::vector<float> theAlphasRms(fPuppiContainer->puppiAlphasRMS());
    std::vector<float> alphas(fPuppiContainer->puppiRawAlphas());
    float nalgos(fPuppiContainer->puppiNAlgos());

    iEvent.emplace(ptokenRawAlphas_, alphas);
    iEvent.emplace(ptokenNalgos_, nalgos);
    iEvent.emplace(ptokenAlphas_, theAlphas);
    iEvent.emplace(ptokenAlphasMed_, theAlphasMed);
    iEvent.emplace(ptokenAlphasRms_, theAlphasRms);
  }
}

// ------------------------------------------------------------------------------------------
void PuppiProducer::beginJob() {}
// ------------------------------------------------------------------------------------------
void PuppiProducer::endJob() {}
// ------------------------------------------------------------------------------------------
void PuppiProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<bool>("puppiDiagnostics", false);
  desc.add<bool>("puppiNoLep", false);
  desc.add<bool>("UseFromPVLooseTight", false);
  desc.add<bool>("UseDeltaZCut", true);
  desc.add<double>("DeltaZCut", 0.3);
  desc.add<double>("EtaMinUseDeltaZ", 0.);
  desc.add<double>("PtMaxCharged", -1.);
  desc.add<double>("EtaMaxCharged", 99999.);
  desc.add<double>("PtMaxPhotons", -1.);
  desc.add<double>("EtaMaxPhotons", 2.5);
  desc.add<double>("PtMaxNeutrals", 200.);
  desc.add<double>("PtMaxNeutralsStartSlope", 0.);
  desc.add<bool>("useExistingWeights", false);
  desc.add<bool>("clonePackedCands", false);
  desc.add<int>("vtxNdofCut", 4);
  desc.add<double>("vtxZCut", 24);
  desc.add<edm::InputTag>("candName", edm::InputTag("particleFlow"));
  desc.add<edm::InputTag>("vertexName", edm::InputTag("offlinePrimaryVertices"));
  desc.add<bool>("applyCHS", true);
  desc.add<bool>("invertPuppi", false);
  desc.add<bool>("useExp", false);
  desc.add<double>("MinPuppiWeight", .01);

  PuppiAlgo::fillDescriptionsPuppiAlgo(desc);

  descriptions.add("PuppiProducer", desc);
}
//define this as a plug-in
DEFINE_FWK_MODULE(PuppiProducer);
