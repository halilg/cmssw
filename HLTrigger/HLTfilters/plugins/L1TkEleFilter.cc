/** \class L1TkEleFilter
 *
 * See header file for documentation
 *
 *
 *  \author Martin Grunewald
 *
 */

#include "L1TkEleFilter.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"

#include "DataFormats/Common/interface/Handle.h"

#include "DataFormats/Common/interface/Ref.h"
#include "DataFormats/HLTReco/interface/TriggerFilterObjectWithRefs.h"
#include "DataFormats/HLTReco/interface/TriggerTypeDefs.h"

#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/EventSetupRecord.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

//
// constructors and destructor
//

L1TkEleFilter::L1TkEleFilter(const edm::ParameterSet& iConfig)
    : HLTFilter(iConfig),
      l1TkEleTag1_(iConfig.getParameter<edm::InputTag>("inputTag1")),
      l1TkEleTag2_(iConfig.getParameter<edm::InputTag>("inputTag2")),
      l1TkEleScalingTag_(iConfig.getParameter<edm::ESInputTag>("esScalingTag")),
      tkEleToken1_(consumes<TkEleCollection>(l1TkEleTag1_)),
      tkEleToken2_(consumes<TkEleCollection>(l1TkEleTag2_)),
      scalingToken_(esConsumes<L1TObjScalingConstants, L1TObjScalingRcd>(l1TkEleScalingTag_)) {
  min_Pt_ = iConfig.getParameter<double>("MinPt");
  min_N_ = iConfig.getParameter<int>("MinN");
  min_Eta_ = iConfig.getParameter<double>("MinEta");
  max_Eta_ = iConfig.getParameter<double>("MaxEta");
  quality1_ = iConfig.getParameter<int>("Quality1");
  quality2_ = iConfig.getParameter<int>("Quality2");
  qual1IsMask_ = iConfig.getParameter<bool>("Qual1IsMask");
  qual2IsMask_ = iConfig.getParameter<bool>("Qual2IsMask");
}

L1TkEleFilter::~L1TkEleFilter() = default;

//
// member functions
//

void L1TkEleFilter::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  makeHLTFilterDescription(desc);
  desc.add<double>("MinPt", -1.0);
  desc.add<double>("MinEta", -5.0);
  desc.add<double>("MaxEta", 5.0);
  desc.add<int>("MinN", 1);
  desc.add<edm::InputTag>("inputTag1", edm::InputTag("L1TkElectrons1"));
  desc.add<edm::InputTag>("inputTag2", edm::InputTag("L1TkElectrons2"));
  desc.add<edm::ESInputTag>("esScalingTag", edm::ESInputTag("L1TScalingESSource", "L1TkEleScaling"));
  desc.add<int>("Quality1", 0);
  desc.add<int>("Quality2", 0);
  desc.add<bool>("Qual1IsMask", false);
  desc.add<bool>("Qual2IsMask", false);
  descriptions.add("L1TkEleFilter", desc);
}

// ------------ method called to produce the data  ------------
bool L1TkEleFilter::hltFilter(edm::Event& iEvent,
                              const edm::EventSetup& iSetup,
                              trigger::TriggerFilterObjectWithRefs& filterproduct) const {
  using namespace std;
  using namespace edm;
  using namespace reco;
  using namespace trigger;

  // All HLT filters must create and fill an HLT filter object,
  // recording any reconstructed physics objects satisfying (or not)
  // this HLT filter, and place it in the Event.

  // The filter object
  if (saveTags()) {
    filterproduct.addCollectionTag(l1TkEleTag1_);
    filterproduct.addCollectionTag(l1TkEleTag2_);
  }

  // Specific filter code

  // get hold of products from Event

  /// Barrel colleciton
  Handle<l1t::TkElectronCollection> tkEles1;
  iEvent.getByToken(tkEleToken1_, tkEles1);

  /// Endcap collection
  Handle<l1t::TkElectronCollection> tkEles2;
  iEvent.getByToken(tkEleToken2_, tkEles2);

  // get scaling constants
  // do we *need* to get these every event? can't we cache them somewhere?
  edm::ESHandle<L1TObjScalingConstants> scalingConstants_ = iSetup.getHandle(scalingToken_);

  // trkEle
  int ntrkEle(0);
  auto atrkEles(tkEles1->begin());
  auto otrkEles(tkEles1->end());
  TkEleCollection::const_iterator itkEle;
  for (itkEle = atrkEles; itkEle != otrkEles; itkEle++) {
    double offlinePt = this->TkEleOfflineEt(itkEle->pt(), itkEle->eta(), *scalingConstants_);
    bool passQuality(false);
    if (qual1IsMask_)
      passQuality = (itkEle->EGRef()->hwQual() & quality1_);
    else
      passQuality = (itkEle->EGRef()->hwQual() == quality1_);

    if (offlinePt >= min_Pt_ && itkEle->eta() <= max_Eta_ && itkEle->eta() >= min_Eta_ && passQuality) {
      ntrkEle++;
      l1t::TkElectronRef ref1(l1t::TkElectronRef(tkEles1, distance(atrkEles, itkEle)));
      filterproduct.addObject(trigger::TriggerObjectType::TriggerL1tkEle, ref1);
    }
  }
  // final filter decision:
  const bool accept(ntrkEle >= min_N_);

  atrkEles = tkEles2->begin();
  otrkEles = tkEles2->end();
  for (itkEle = atrkEles; itkEle != otrkEles; itkEle++) {
    double offlinePt = this->TkEleOfflineEt(itkEle->pt(), itkEle->eta(), *scalingConstants_);
    bool passQuality(false);
    if (qual2IsMask_)
      passQuality = (itkEle->EGRef()->hwQual() & quality2_);
    else
      passQuality = (itkEle->EGRef()->hwQual() == quality2_);

    if (offlinePt >= min_Pt_ && itkEle->eta() <= max_Eta_ && itkEle->eta() >= min_Eta_ && passQuality) {
      ntrkEle++;
      l1t::TkElectronRef ref2(l1t::TkElectronRef(tkEles2, distance(atrkEles, itkEle)));
      filterproduct.addObject(trigger::TriggerObjectType::TriggerL1tkEle, ref2);
    }
  }
  const bool accept2(ntrkEle >= min_N_);

  // return with final filter decision
  return (accept and accept2);
}

double L1TkEleFilter::TkEleOfflineEt(double Et, double Eta, const L1TObjScalingConstants& scalingConstants) const {
  if (std::abs(Eta) < 1.5)
    return (scalingConstants.m_constants.at(0).m_constant + Et * scalingConstants.m_constants.at(0).m_linear +
            Et * Et * scalingConstants.m_constants.at(0).m_quadratic);
  else
    return (scalingConstants.m_constants.at(1).m_constant + Et * scalingConstants.m_constants.at(1).m_linear +
            Et * Et * scalingConstants.m_constants.at(1).m_quadratic);
}
