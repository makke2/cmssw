#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventPrincipal.h"
#include "FWCore/Framework/interface/ConsumesCollector.h"
#include "FWCore/Framework/interface/ProducerBase.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "DataFormats/Common/interface/Handle.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingVertex.h"

#include "PreMixingWorker.h"

namespace edm {
  class PreMixingTrackingParticleWorker: public PreMixingWorker {
  public:
    PreMixingTrackingParticleWorker(const edm::ParameterSet& ps, edm::ProducerBase& producer, edm::ConsumesCollector && iC);
    ~PreMixingTrackingParticleWorker() override = default;
    
    void initializeEvent(edm::Event const& iEvent, edm::EventSetup const& iSetup) override;
    void addSignals(edm::Event const& iEvent, edm::EventSetup const& iSetup) override;
    void addPileups(int bcr, edm::EventPrincipal const& ep, int eventNr, edm::EventSetup const& iSetup, edm::ModuleCallingContext const *mcc) override;
    void put(edm::Event& iEvent, edm::EventSetup const& iSetup, std::vector<PileupSummaryInfo> const& ps, int bunchSpacing) override;

  private:
    void add(const std::vector<TrackingParticle>& particles, const std::vector<TrackingVertex>& vertices);
    
    edm::EDGetTokenT<std::vector<TrackingParticle> > TrackSigToken_;  // Token to retrieve information  
    edm::EDGetTokenT<std::vector<TrackingVertex> > VtxSigToken_;  // Token to retrieve information  

    edm::InputTag TrackingParticlePileInputTag_ ;    // InputTag for pileup tracks

    std::string TrackingParticleCollectionDM_; // secondary name to be given to new TrackingParticle


    std::unique_ptr<std::vector<TrackingParticle>> NewTrackList_;
    std::unique_ptr<std::vector<TrackingVertex>> NewVertexList_;
    TrackingParticleRefProd TrackListRef_;
    TrackingVertexRefProd VertexListRef_;
  };

  PreMixingTrackingParticleWorker::PreMixingTrackingParticleWorker(const edm::ParameterSet& ps, edm::ProducerBase& producer, edm::ConsumesCollector && iC):
    TrackSigToken_(iC.consumes<std::vector<TrackingParticle> >(ps.getParameter<edm::InputTag>("labelSig"))),
    VtxSigToken_  (iC.consumes<std::vector<TrackingVertex>   >(ps.getParameter<edm::InputTag>("labelSig"))),
    TrackingParticlePileInputTag_(ps.getParameter<edm::InputTag>("pileInputTag")),
    TrackingParticleCollectionDM_(ps.getParameter<std::string>("collectionDM"))
  {
    producer.produces< std::vector<TrackingParticle> >(TrackingParticleCollectionDM_);
    producer.produces< std::vector<TrackingVertex> >(TrackingParticleCollectionDM_);
  }


  void PreMixingTrackingParticleWorker::initializeEvent(edm::Event const& iEvent, edm::EventSetup const& iSetup) {
    NewTrackList_ = std::make_unique<std::vector<TrackingParticle>>();
    NewVertexList_ = std::make_unique<std::vector<TrackingVertex>>();

    // need RefProds in order to re-key the particle<->vertex refs
    // TODO: try to remove const_cast, requires making Event non-const in BMixingModule::initializeEvent
    TrackListRef_  = const_cast<edm::Event&>(iEvent).getRefBeforePut< std::vector<TrackingParticle> >(TrackingParticleCollectionDM_);
    VertexListRef_ = const_cast<edm::Event&>(iEvent).getRefBeforePut< std::vector<TrackingVertex> >(TrackingParticleCollectionDM_);    
  }

  void PreMixingTrackingParticleWorker::addSignals(edm::Event const& iEvent, edm::EventSetup const& iSetup) {
    edm::Handle<std::vector<TrackingParticle>> tracks;
    iEvent.getByToken(TrackSigToken_, tracks);

    edm::Handle<std::vector<TrackingVertex>> vtxs;
    iEvent.getByToken(VtxSigToken_, vtxs);

    if(tracks.isValid() && vtxs.isValid()) {
      add(*tracks, *vtxs);
    }
  }

  void PreMixingTrackingParticleWorker::addPileups(int bcr, edm::EventPrincipal const& ep, int eventNr, edm::EventSetup const& iSetup, edm::ModuleCallingContext const *mcc) {
    LogDebug("PreMixingTrackingParticleWorker") <<"\n===============> adding pileups from event  "<<ep.id()<<" for bunchcrossing "<<bcr;
                                                                                                                                    
    std::shared_ptr<Wrapper<std::vector<TrackingParticle> >  const> inputPTR =
      getProductByTag<std::vector<TrackingParticle> >(ep, TrackingParticlePileInputTag_, mcc);

    std::shared_ptr<Wrapper<std::vector<TrackingVertex> >  const> inputVPTR =
      getProductByTag<std::vector<TrackingVertex> >(ep, TrackingParticlePileInputTag_, mcc);

    if(inputPTR && inputVPTR) {
      add(*(inputPTR->product()), *(inputVPTR->product()));
    }
  }

  void PreMixingTrackingParticleWorker::add(const std::vector<TrackingParticle>& particles, const std::vector<TrackingVertex>& vertices) {
    const size_t StartingIndexV = NewVertexList_->size();
    const size_t StartingIndexT = NewTrackList_->size();

    // grab Vertices, store copy, preserving indices.  Easier to loop over vertices first - fewer links
    for(const auto& vtx: vertices) {
      NewVertexList_->push_back(vtx);
    }

    // grab tracks, store copy
    for(const auto& track: particles) {
      const auto& oldRef=track.parentVertex();
      auto newRef=TrackingVertexRef( VertexListRef_, oldRef.index()+StartingIndexV );
      NewTrackList_->push_back(track);

      auto & Ntrack = NewTrackList_->back();  //modify copy

      Ntrack.setParentVertex( newRef );
      Ntrack.clearDecayVertices();

      // next, loop over daughter vertices, same strategy
      for( auto const& vertexRef : track.decayVertices() ) {
        auto newRef=TrackingVertexRef( VertexListRef_, vertexRef.index()+StartingIndexV );
        Ntrack.addDecayVertex(newRef);
      }
    }

    // Now that tracks are handled, go back and put correct Refs in vertices
    // Operate only on the added pileup vertices, and leave the already-existing vertices untouched
    std::vector<decltype(TrackingParticleRef().index())> sourceTrackIndices;
    std::vector<decltype(TrackingParticleRef().index())> daughterTrackIndices;
    for(size_t iVertex = StartingIndexV; iVertex != NewVertexList_->size(); ++iVertex) {
      auto& vertex = (*NewVertexList_)[iVertex];

      // Need to copy the indices before clearing the vectors
      sourceTrackIndices.reserve(vertex.sourceTracks().size());
      daughterTrackIndices.reserve(vertex.daughterTracks().size());
      for(auto const& ref: vertex.sourceTracks()) sourceTrackIndices.push_back(ref.index());
      for(auto const& ref: vertex.daughterTracks()) daughterTrackIndices.push_back(ref.index());

      vertex.clearParentTracks();
      vertex.clearDaughterTracks();

      for( auto index : sourceTrackIndices ) {
        auto newRef=TrackingParticleRef( TrackListRef_, index+StartingIndexT );
        vertex.addParentTrack(newRef);
      }

      // next, loop over daughter tracks, same strategy                                                                 
      for( auto index : daughterTrackIndices ) {
        auto newRef=TrackingParticleRef( TrackListRef_, index+StartingIndexT );
        vertex.addDaughterTrack(newRef);
      }

      sourceTrackIndices.clear();
      daughterTrackIndices.clear();
    }
  }
  
  void PreMixingTrackingParticleWorker::put(edm::Event& iEvent, edm::EventSetup const& iSetup, std::vector<PileupSummaryInfo> const& ps, int bunchSpacing) {
    LogInfo("PreMixingTrackingParticleWorker") << "total # Merged Tracks: " << NewTrackList_->size() ;
    iEvent.put(std::move(NewTrackList_), TrackingParticleCollectionDM_);
    iEvent.put(std::move(NewVertexList_), TrackingParticleCollectionDM_);
  }
}

#include "PreMixingWorkerFactory.h"
DEFINE_EDM_PLUGIN(PreMixingWorkerFactory, edm::PreMixingTrackingParticleWorker, "PreMixingTrackingParticleWorker");
