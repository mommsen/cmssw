#include "L1Trigger/L1THGCal/interface/HGCalTriggerBackendAlgorithmBase.h"
#include "L1Trigger/L1THGCal/interface/fe_codecs/HGCalTriggerCellBestChoiceCodec.h"
#include "L1Trigger/L1THGCal/interface/fe_codecs/HGCalTriggerCellThresholdCodec.h"
#include "DataFormats/ForwardDetId/interface/HGCalDetId.h"
#include "DataFormats/L1THGCal/interface/HGCalTriggerCell.h"
#include "L1Trigger/L1THGCal/interface/be_algorithms/HGCalTriggerCellCalibration.h"
#include "DataFormats/L1THGCal/interface/HGCalCluster.h"
#include "DataFormats/L1THGCal/interface/HGCalMulticluster.h"

using namespace HGCalTriggerBackend;

template<typename FECODEC, typename DATA>
class HGCClusterAlgo : public Algorithm<FECODEC> 
{
    public:
        using Algorithm<FECODEC>::name;

    protected:
        using Algorithm<FECODEC>::codec_;

    public:
        HGCClusterAlgo(const edm::ParameterSet& conf, edm::ConsumesCollector& cc):
            Algorithm<FECODEC>(conf,cc),
            cluster_product_( new l1t::HGCalClusterBxCollection ),
            cluster3D_product_( new l1t::HGCalMulticlusterBxCollection ),
            HGCalEESensitive_(conf.getParameter<std::string>("HGCalEESensitive_tag")),
            HGCalHESiliconSensitive_(conf.getParameter<std::string>("HGCalHESiliconSensitive_tag")),
            calibration_(conf.getParameterSet("calib_parameters")),
            seed_CUT_(conf.getParameter<double>("seeding_threshold")), 
            tc_CUT_(conf.getParameter<double>("clustering_threshold")),
            dR_forC3d_(conf.getParameter<double>("dR_searchNeighbour")){}
           
        typedef std::unique_ptr<HGCalTriggerGeometryBase> ReturnType;

        virtual void setProduces(edm::EDProducer& prod) const override final 
        {
            prod.produces<l1t::HGCalClusterBxCollection>(name());
            prod.produces<l1t::HGCalMulticlusterBxCollection>("cluster3D");

        }

        virtual void run(const l1t::HGCFETriggerDigiCollection& coll, const edm::EventSetup& es, const edm::Event&evt ) override final;
        virtual void putInEvent(edm::Event& evt) override final 
        {
            evt.put(std::move(cluster_product_),name());
            evt.put(std::move(cluster3D_product_),"cluster3D");
        }

        virtual void reset() override final 
        {
            cluster_product_.reset( new l1t::HGCalClusterBxCollection );            
            cluster3D_product_.reset( new l1t::HGCalMulticlusterBxCollection );            
        }

    private:

        std::unique_ptr<l1t::HGCalClusterBxCollection> cluster_product_;
        std::unique_ptr<l1t::HGCalMulticlusterBxCollection> cluster3D_product_;
        std::string HGCalEESensitive_;
        std::string HGCalHESiliconSensitive_;

        edm::ESHandle<HGCalTopology> hgceeTopoHandle_;
        edm::ESHandle<HGCalTopology> hgchefTopoHandle_;
        HGCalTriggerCellCalibration calibration_;    
        double seed_CUT_;
        double tc_CUT_;
        double dR_forC3d_;

        double deltaPhi( double phi1, double phi2) {
        
           double dPhi(phi1-phi2);
           double pi(acos(-1.0));
           if     (dPhi<=-pi) dPhi+=2.0*pi;
           else if(dPhi> pi) dPhi-=2.0*pi;
        
           return dPhi;
        }
    

        double deltaEta(double eta1, double eta2){
           double dEta = (eta1-eta2);
           return dEta;
        }

        double deltaR(double eta1, double eta2, double phi1, double phi2) {
           double dEta = deltaEta(eta1, eta2);
           double dPhi = deltaPhi(phi1, phi2);
           return sqrt(dEta*dEta+dPhi*dPhi);
        }
};


/*****************************************************************/
template<typename FECODEC, typename DATA>
void HGCClusterAlgo<FECODEC,DATA>::run(const l1t::HGCFETriggerDigiCollection& coll, 
                                       const edm::EventSetup& es,
                                       const edm::Event&evt
    ) 
/*****************************************************************/
{
    //virtual void run(const l1t::HGCFETriggerDigiCollection& coll, const edm::EventSetup& es, const edm::Event&evt) override final
    //   {
            es.get<IdealGeometryRecord>().get(HGCalEESensitive_, hgceeTopoHandle_);
            es.get<IdealGeometryRecord>().get(HGCalHESiliconSensitive_, hgchefTopoHandle_);
            std::cout << "CLUSTERING PARAMETERS: "<< std::endl;
            std::cout << "------ Clustering thresholds for trigger cells to be included in C2d: " << tc_CUT_ << std::endl;
            std::cout << "------ Seeding thresholds to start the clusterization procedure: " << seed_CUT_ << std::endl; 
            std::cout << "------ Max-Distance in the normalized plane to search for next-layer C2d to merge into the C3d: " << dR_forC3d_ << std::endl;  
            
            for( const auto& digi : coll ) 
            {

                DATA data;
                data.reset();
                const HGCalDetId& module_id = digi.getDetId<HGCalDetId>();
                digi.decode(codec_, data);
                
                double_t moduleEta = 0.;
                double_t modulePhi = 0.;           
                double_t C2d_pt  = 0.;
                double_t C2d_eta = 0.;
                double_t C2d_phi = 0.;
                uint32_t C2d_hwPtEm = 0;
                uint32_t C2d_hwPtHad = 0;
                for(const auto& triggercell : data.payload)
                {
                    if(triggercell.hwPt()>0)
                    {
                        
                        HGCalDetId detid(triggercell.detId());
                        int subdet = detid.subdetId();
                        int cellThickness = 0;
                        
                        if( subdet == HGCEE ){ 
                            cellThickness = (hgceeTopoHandle_)->dddConstants().waferTypeL((unsigned int)detid.wafer() );
                        }else if( subdet == HGCHEF ){
                            cellThickness = (hgchefTopoHandle_)->dddConstants().waferTypeL((unsigned int)detid.wafer() );
                        }else if( subdet == HGCHEB ){
                            edm::LogWarning("DataNotFound") << "ATTENTION: the BH trgCells are not yet implemented !! ";
                        }
              
                        if(module_id.layer()<28){
                            C2d_hwPtEm+=triggercell.hwPt();
                        }else if(module_id.layer()>=28){
                            C2d_hwPtHad+=triggercell.hwPt();
                        }
              
                        l1t::HGCalTriggerCell calibratedtriggercell(triggercell);
                        calibration_.calibrate(calibratedtriggercell, cellThickness);     
                        C2d_pt += calibratedtriggercell.pt();                        
                        moduleEta += calibratedtriggercell.pt()*calibratedtriggercell.eta();
                        modulePhi += calibratedtriggercell.pt()*calibratedtriggercell.phi();
                        //CODE THE REAL C2D-ALGORITHM HERE: using trg-cells + neighbours info
                    }
                }
                l1t::HGCalCluster cluster( reco::LeafCandidate::LorentzVector(), C2d_hwPtEm + C2d_hwPtHad, 0, 0);
                cluster.setModule(module_id.wafer());
                cluster.setLayer(module_id.layer());
                cluster.setSubDet(module_id.subdetId());
                cluster.setHwPtEm(C2d_hwPtEm);
                cluster.setHwPtHad(C2d_hwPtHad);

                if((cluster.hwPtEm()+cluster.hwPtHad())>tc_CUT_+8){
                    C2d_eta = moduleEta/C2d_pt;
                    C2d_phi = modulePhi/C2d_pt;                
                    math::PtEtaPhiMLorentzVector calibP4(C2d_pt, C2d_eta, C2d_phi, 0 );
                    cluster.setP4(calibP4);
                    cluster_product_->push_back(0,cluster);
                    std::cout << "Energy of the uncalibrated cluster " << C2d_hwPtEm + C2d_hwPtHad << "  with EM-pt() = " << cluster.hwPtEm()<< " had-pt = "<<cluster.hwPtHad() <<"   id-module " << cluster.module() << "  layer " << cluster.layer() << std::endl ; //use pt and not pt()
                    std::cout << "    ----> 4P of C2d (pt,eta,phi,M) = " << cluster.p4().Pt()<<", " << cluster.p4().Eta() << ", " << cluster.p4().Phi() << ", " << cluster.p4().M() << std::endl;
                }
            }
            //CODE THE REAL C3D-ALGORITHM HERE: using previous built C2D  + fill all information transmittable to the CORRELATOR                
            std::vector<size_t> isMerged;
            
            size_t seedx=0;
            for(l1t::HGCalClusterBxCollection::const_iterator c2d = cluster_product_->begin(); c2d != cluster_product_->end(); ++c2d, ++seedx){
                l1t::HGCalMulticluster cluster3D( reco::LeafCandidate::LorentzVector(), 0, 0, 0);
                double_t tmpEta = 0.;
                double_t tmpPhi = 0.;           
                double_t C3d_pt  = 0.;
                double_t C3d_eta = 0.;
                double_t C3d_phi = 0.;
                uint32_t C3d_hwPtEm = 0;
                uint32_t C3d_hwPtHad = 0;
                uint32_t totLayer = 0;

                bool skip=false;

                std::cout << "In the C2d collection, seed the C3d with this : " << seedx << " - "<< c2d->p4().Pt() << " eta: " <<  c2d->p4().Eta() << " --> layer " << c2d->layer() << "  skip before 2nd loop "<< skip << std::endl;                
                
                size_t idx=0;
                for(l1t::HGCalClusterBxCollection::const_iterator c2d_aux = cluster_product_->begin(); c2d_aux != cluster_product_->end(); ++c2d_aux, ++idx){
                    std::cout << "     loop over C2d again and search for match:" << "   idx: " << idx << "  eta: " << c2d_aux->p4().Eta() << std::endl;
                    std::cout << "   before isMerged loop: " << skip<< std::endl;
                    for(size_t i(0); i<isMerged.size(); i++){
                        std::cout <<  isMerged.at(i) << ", ";
                        if(idx==isMerged.at(i)){
                            skip=true;
                            continue;
                        }
                    }
                    std::cout << "\n";
                    double dR =  deltaR( c2d->p4().Eta(), c2d_aux->p4().Eta(), c2d->p4().Phi(), c2d_aux->p4().Phi() ); 
//                    std::cout << "looping on the c2d directly from the collection : "<< c2d->p4().pt() << "  --> layer " << c2d->layer() << " dR: " << dR << "  SKIP var = " << skip << std::endl;

                    if(skip){
                        skip=false;
                        //std::cout << "     the c2d considered has been already merged!!";
                        continue;
                    }
                    if( dR < 0.1 ){
                                              std::cout << "     The idx "<< idx << " C2d has been matched and kept for 3D to the " << seedx 
                                  << " - "<< c2d_aux->p4().Pt() << " eta: " <<  c2d_aux->p4().Eta() 
                                  << " --> layer " << c2d_aux->layer() << std::endl;             
                        isMerged.push_back(idx);
                        tmpEta+=c2d_aux->p4().Eta() * c2d_aux->p4().Pt();
                        tmpPhi+=c2d_aux->p4().Phi() * c2d_aux->p4().Pt();
                        C3d_pt+=c2d_aux->p4().Pt();
                        C3d_hwPtEm+=c2d_aux->hwPtEm();
                        C3d_hwPtHad+=c2d_aux->hwPtHad();
                        totLayer++;
                    }
                }
                
                std::cout <<"STO PER ENTRARE NEL MULTICLUSTERING" << std::endl;
                if( totLayer > 2){
                    cluster3D.setNtotLayer(totLayer);
                    cluster3D.setHwPtEm(C3d_hwPtEm);
                    cluster3D.setHwPtHad(C3d_hwPtHad);
                    C3d_eta=tmpEta/C3d_pt;
                    C3d_phi=tmpPhi/C3d_pt;                
                    math::PtEtaPhiMLorentzVector calib3dP4(C3d_pt, C3d_eta, C3d_phi, 0 );
                    cluster3D.setP4(calib3dP4);                    
                    std::cout << "  A MULTICLUSTER has been built with pt, eta, phi = " << C3d_pt << ", " << C3d_eta << ", "<< C3d_phi <<  std::endl;
                    cluster3D_product_->push_back(0,cluster3D);
                }

                
            }
}
    



typedef HGCClusterAlgo<HGCalTriggerCellBestChoiceCodec, HGCalTriggerCellBestChoiceCodec::data_type> HGCClusterAlgoBestChoice;
typedef HGCClusterAlgo<HGCalTriggerCellThresholdCodec, HGCalTriggerCellThresholdCodec::data_type> HGCClusterAlgoThreshold;

DEFINE_EDM_PLUGIN(HGCalTriggerBackendAlgorithmFactory, 
        HGCClusterAlgoBestChoice,
        "HGCClusterAlgoBestChoice");

DEFINE_EDM_PLUGIN(HGCalTriggerBackendAlgorithmFactory, 
        HGCClusterAlgoThreshold,
        "HGCClusterAlgoThreshold");
