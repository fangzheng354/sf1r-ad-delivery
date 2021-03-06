#include <common/Utilities.h>
#include <sstream>      // std::stringstream, std::stringbuf

#include "LaserManager.h"
#include "clusteringmanager/clustering/PCARunner.h"
#include "LaserIndexTask.h"
#include "LaserRpcServer.h"
#include <mining-manager/MiningManager.h>
#include <query-manager/ActionItem.h>
#include <ad-manager/AdSearchService.h>
#include <glog/logging.h>
#include <sys/time.h>

using namespace sf1r::laser;
namespace sf1r
{

const static std::size_t THREAD_NUM = 8;
    
laser::LaserRpcServer* LaserManager::rpcServer_ = NULL;
boost::shared_mutex LaserManager::mutex_;

LaserManager::LaserManager(const boost::shared_ptr<AdSearchService>& adSearchService, 
        const boost::shared_ptr<DocumentManager>& documentManager,
        izenelib::ir::idmanager::IDManager& idManager,
        const std::string& collectionDataPath,
        const std::string& collection,
        const LaserPara& para)
    : collection_(collection)
    , workdir_(collectionDataPath + "/LASER/")
    , sysdir_(MiningManager::system_working_path_ + "/LASER/" + collection_)
    , para_(para)
    , adSearchService_(adSearchService)
    , documentManager_(documentManager)
    , idManager_(idManager)
    , recommend_(NULL)
    , indexManager_(NULL)
    , tokenizer_(NULL)
    , clusteringContainer_(NULL)
    , similarClustering_(NULL)
{
    resdir_ = MiningManager::system_resource_path_ + "/laser_resource/";
    loadLaserDependency(); 
    
    if (!boost::filesystem::exists(workdir_))
    {
        boost::filesystem::create_directories(workdir_);
    }
    if (!boost::filesystem::exists(sysdir_))
    {
        boost::filesystem::create_directories(sysdir_);
    }

    
    tokenizer_ = new Tokenizer(MiningManager::system_resource_path_ + "/dict/title_pca/",
        resdir_ + "/terms_dic.dat");

    bool isNeedClustering = isNeedClusteringKnowlege();
    if (isNeedClustering)
    {
        LOG(INFO)<<"Clustering is enbale, load clustering knowledge...";
        loadClusteringKnowledge();
    }
    
    indexManager_ = new laser::AdIndexManager(workdir_, 
        isNeedClustering,
        this);
    recommend_ = new LaserRecommend(this);
    // delete by TaskBuilder
    indexTask_ = new LaserIndexTask(this);

    LaserManager::rpcServer_->registerCollection(collection_, this);

}
   
LaserManager::~LaserManager()
{
    LaserManager::rpcServer_->removeCollection(collection_);
    if (NULL != indexManager_)
    {
        delete indexManager_;
        indexManager_ = NULL;
    }
    if (NULL != recommend_)
    {
        delete recommend_;
        recommend_ = NULL;
    }
    if (NULL != tokenizer_)
    {
        delete tokenizer_;
        tokenizer_ = NULL;
    }
    if (NULL != clusteringContainer_)
    {
        delete clusteringContainer_;
        clusteringContainer_ = NULL;
    }
    if (NULL != similarClustering_)
    {
        delete similarClustering_;
        similarClustering_ = NULL;
    }
}

bool LaserManager::recommend(const LaserRecommendParam& param, 
    GetDocumentsByIdsActionItem& actionItem,
    RawTextResultFromAIA& res) const 
{
    std::vector<docid_t> docIdList;
    std::vector<float> itemScoreList;
    LOG(INFO)<<"recommend_";
    if (!recommend_->recommend(param.text_, docIdList, itemScoreList, param.topN_))
    {
        res.error_ = "Internal ERROR in Recommend Engine, no data for : " + param.text_;;
        return false;
    }
    for (std::size_t i = 0; i < docIdList.size(); ++i)
    {
        actionItem.idList_.push_back(docIdList[i]);
    }
    LOG(INFO)<<"recommend finish";
    boost::posix_time::ptime stime = boost::posix_time::microsec_clock::local_time();
    adSearchService_->getDocumentsByIds(actionItem, res);
    boost::posix_time::ptime etime = boost::posix_time::microsec_clock::local_time();
    LOG(INFO)<<"get ad time = "<<(etime-stime).total_milliseconds();
    res.score_.swap(itemScoreList);
    return true;
}
    
void LaserManager::index(const docid_t& docid, const std::string& title)
{
    TokenSparseVector vec;
    tokenizer_->tokenize(title, vec);
    
    if (NULL != clusteringContainer_)
    {
        std::size_t clusteringId = assignClustering_(vec);
        if ( (std::size_t)-1 == clusteringId)
        {
            clusteringId = rand() % clusteringContainer_->size();
        }
        indexManager_->index(clusteringId, docid, vec); 
    }
    else
    {
        indexManager_->index(docid, vec);
    }
}
    
MiningTask* LaserManager::getLaserIndexTask()
{
    return indexTask_;
}
    
const std::size_t LaserManager::getClustering(const std::string& title) const
{
    TokenSparseVector vec;
    tokenizer_->tokenize(title, vec);
    return assignClustering_(vec);
}

std::size_t LaserManager::assignClustering_(const TokenSparseVector& v) const
{
    if (v.empty())
    {
        return -1;
    }
    std::size_t size = clusteringContainer_->size();
    
    float maxSim = 0.0;
    std::size_t maxId = -1;
    for (std::size_t i = 0; i < size; ++i)
    {
        float sim = similarity_(v, (*clusteringContainer_)[i]);
        if (sim > maxSim)
        {
            maxSim = sim;
            maxId = i;
        }
    }
    return maxId;
}
    
float LaserManager::similarity_(const TokenSparseVector& lv, const TokenVector& rv) const
{
    float sim = 0.0;
    TokenSparseVector::const_iterator it = lv.begin(); 
    for (; it != lv.end(); ++it)
    {
        sim += rv[it->first] * it->second;
    }
    return sim;
}

bool LaserManager::convertDocId(
    const std::string& docStr,
    docid_t& docId) const
{
    uint128_t convertId = Utilities::md5ToUint128(docStr);

    if (!idManager_.getDocIdByDocName(convertId, docId, false))
    {
        return false;
    }
    return true;
}
    
bool LaserManager::getAdInfoByDOCID(const std::string& DOCID, 
    std::string& clusteringId,
    std::vector<int>& index, 
    std::vector<float>& value) const
{
    docid_t docid = 0;
    if (convertDocId(DOCID, docid))
    {
        std::vector<std::pair<int, float> > vec;
        if (indexManager_->get(docid, vec))
        {
            std::vector<std::pair<int, float> >::const_iterator it = vec.begin();
            for (; it != vec.end(); ++it)
            {
                index.push_back(it->first);
                value.push_back(it->second);
            }
        }
        std::size_t cid = 0;
        indexManager_->get(docid, cid);
        std::stringstream ss;
        ss <<cid;
        clusteringId = ss.str() + "|clustering";
        return true;
    }
    return false;
}
    
void LaserManager::tokenize(const std::string& text, 
   std::vector<int>& index, 
   std::vector<float>& value) const
{
    std::vector<std::pair<int, float> > vec;
    tokenizer_->tokenize(text, vec);
    std::vector<std::pair<int, float> >::const_iterator it = vec.begin();
    for (; it != vec.end(); ++it)
    {
        index.push_back(it->first);
        value.push_back(it->second);
    }
}

bool LaserManager::isNeedClusteringKnowlege() const
{
    if ("TopnClusteringModel" == para_.modelType)
    {
        return true;
    }
    if ("HierarchicalModel" == para_.modelType)
    {
        return true;
    }
    return false;
}
    
void LaserManager::loadClusteringKnowledge()
{
    std::vector<boost::unordered_map<std::string, float> > clusteringContainer;
    laser::clustering::loadClusteringResult(clusteringContainer, resdir_ + "/clustering_result");
    
    clusteringContainer_ = new std::vector<LaserManager::TokenVector>(clusteringContainer.size());
    for (std::size_t i = 0; i < clusteringContainer.size(); ++i)
    {
        TokenVector vec;
        tokenizer_->numeric(clusteringContainer[i], vec);
        (*clusteringContainer_)[i] = vec;
    }
   
    similarClustering_ = new std::vector<std::vector<int> >(clusteringContainer_->size());
    {
        std::ifstream ifs((resdir_ + "/clustering_similar").c_str(), std::ios::binary);
        boost::archive::text_iarchive ia(ifs);
        ia >> *similarClustering_;
    }
}
    
void loadLaserDependency()
{
    boost::unique_lock<boost::shared_mutex> uniqueLock(LaserManager::mutex_);
    if (NULL != LaserManager::rpcServer_)
    {
        return;
    }
    LOG(INFO)<<"open all dependencies of LaserManager...";
    LaserManager::rpcServer_ = new LaserRpcServer();
    LaserManager::rpcServer_->start("0.0.0.0", 28611, 2);
}

void closeLaserDependency()
{
    boost::unique_lock<boost::shared_mutex> uniqueLock(LaserManager::mutex_);
    if (NULL == LaserManager::rpcServer_)
    {
        return;
    }
    LOG(INFO)<<"close all dependencies of LaserManager..";
    if (NULL != LaserManager::rpcServer_)
    {
        LaserManager::rpcServer_->stop();
        delete LaserManager::rpcServer_;
        LaserManager::rpcServer_ = NULL;
    }
}


}
